/* Multi-peer UCP acceptance. TCP exchanges startup metadata and addresses only.
 * All test data and start/finish coordination below use UCP TAG operations. */
#define _POSIX_C_SOURCE 200809L

#include <ucp/api/ucp.h>
#include <arpa/inet.h>
#include <endian.h>
#include <errno.h>
#include <netdb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define MAX_CLIENTS 16u
#define MAX_MESSAGE_BYTES (8u * 1024u * 1024u)
#define MAX_ADDRESS_BYTES (1024u * 1024u)
#define BUFFER_ALIGNMENT 4096u
#define BOOTSTRAP_MAGIC 0x524d4d31u
#define REQUEST_TIMEOUT_SECONDS 60.0

enum message_kind {
    CLIENT_DATA = 1,
    SERVER_DATA = 2,
    START_TOKEN = 3,
    DONE_TOKEN = 4,
    FINISH_TOKEN = 5
};

typedef struct {
    unsigned is_server;
    unsigned client_id;
    unsigned clients;
    unsigned iterations;
    const char *host;
    const char *port;
} test_options_t;

typedef struct {
    /* client_id identifies a communication pair in the application's TAGs.
     * peer_id is the remote role: 0 for server, 1..N for clients. */
    unsigned client_id;
    unsigned peer_id;
    ucp_ep_h endpoint;
    unsigned char *send_buffer;
    unsigned char *receive_buffer;
    void *send_request;
    void *receive_request;
    int send_complete;
    int receive_complete;
    ucp_tag_recv_info_t receive_info;
    ucs_status_t receive_status;
    ucs_status_t endpoint_status;
    unsigned error_callbacks;
    uint64_t verified_bytes;
} test_peer_t;

static ucp_worker_h worker;
static test_peer_t *peers;
static unsigned peer_count;
static unsigned local_rank;

static void fail_message(const char *operation)
{
    fprintf(stderr, "FAIL MULTI rank=%u operation=%s\n", local_rank, operation);
    exit(EXIT_FAILURE);
}

static void require_status(ucs_status_t result, const char *operation,
                           const test_peer_t *peer)
{
    if (result == UCS_OK) {
        return;
    }
    fprintf(stderr, "FAIL MULTI rank=%u peer=%d operation=%s status=%s\n",
            local_rank, peer ? (int)peer->peer_id : -1, operation,
            ucs_status_string(result));
    exit(EXIT_FAILURE);
}

static double monotonic_seconds(void)
{
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        fail_message("clock_gettime");
    }
    return now.tv_sec + now.tv_nsec * 1e-9;
}

static unsigned parse_number(const char *text, unsigned maximum)
{
    unsigned long value;
    char *end;

    errno = 0;
    value = strtoul(text, &end, 10);
    if ((errno != 0) || (end == text) || (*end != '\0') ||
        (value == 0) || (value > maximum)) {
        fail_message("invalid numeric argument");
    }
    return (unsigned)value;
}

static test_options_t parse_options(int argc, char **argv)
{
    test_options_t options = {0};
    int argument;

    options.clients = 2;
    options.client_id = 1;
    options.iterations = 1;
    if ((argc >= 3) && !strcmp(argv[1], "--server")) {
        options.is_server = 1;
        options.port = argv[2];
        argument = 3;
    } else if ((argc >= 4) && !strcmp(argv[1], "--client")) {
        options.host = argv[2];
        options.port = argv[3];
        argument = 4;
    } else {
        fprintf(stderr, "usage: %s --server PORT [--peers N] [--iters N]\n"
                        "       %s --client HOST PORT [--id N] [--iters N]\n",
                argv[0], argv[0]);
        exit(2);
    }
    while (argument < argc) {
        if ((argument + 1 >= argc)) {
            fail_message("missing option value");
        }
        if (!strcmp(argv[argument], "--iters")) {
            options.iterations = parse_number(argv[argument + 1], 1000);
        } else if (options.is_server && !strcmp(argv[argument], "--peers")) {
            options.clients = parse_number(argv[argument + 1], MAX_CLIENTS);
        } else if (!options.is_server && !strcmp(argv[argument], "--id")) {
            options.client_id = parse_number(argv[argument + 1], MAX_CLIENTS);
        } else {
            fail_message("unknown option");
        }
        argument += 2;
    }
    return options;
}

static void endpoint_error_callback(void *arg, ucp_ep_h endpoint,
                                     ucs_status_t result)
{
    test_peer_t *peer = arg;

    (void)endpoint;
    ++peer->error_callbacks;
    if (peer->endpoint_status == UCS_OK) {
        peer->endpoint_status = result;
    }
    fprintf(stderr, "MULTI_EP_ERROR rank=%u peer=%u status=%s callbacks=%u\n",
            local_rank, peer->peer_id, ucs_status_string(result),
            peer->error_callbacks);
}

static void check_endpoint_errors(void)
{
    unsigned index;

    for (index = 0; index < peer_count; ++index) {
        require_status(peers[index].endpoint_status, "endpoint_error", &peers[index]);
    }
}

static void receive_callback(void *request, ucs_status_t result,
                              const ucp_tag_recv_info_t *info, void *arg)
{
    test_peer_t *peer = arg;

    (void)request;
    peer->receive_status = result;
    if ((result == UCS_OK) && (info != NULL)) {
        peer->receive_info = *info;
    }
}

static void reset_batch(void)
{
    unsigned index;

    for (index = 0; index < peer_count; ++index) {
        peers[index].send_request = NULL;
        peers[index].receive_request = NULL;
        peers[index].send_complete = 1;
        peers[index].receive_complete = 1;
        peers[index].receive_status = UCS_OK;
        memset(&peers[index].receive_info, 0, sizeof(peers[index].receive_info));
    }
}

static uint64_t make_tag(unsigned kind, unsigned client_id,
                         unsigned size_index, unsigned iteration)
{
    return ((uint64_t)kind << 56) | ((uint64_t)client_id << 48) |
           ((uint64_t)size_index << 32) | iteration;
}

static void post_receive(test_peer_t *peer, size_t length, uint64_t tag)
{
    ucp_request_param_t parameters = {0};

    /* This HUCX version's immediate path does not fill param.recv_info.
     * Keep a completed request and get length/TAG through the callback instead. */
    parameters.op_attr_mask = UCP_OP_ATTR_FIELD_CALLBACK |
                              UCP_OP_ATTR_FIELD_USER_DATA |
                              UCP_OP_ATTR_FLAG_NO_IMM_CMPL;
    parameters.cb.recv = receive_callback;
    parameters.user_data = peer;
    peer->receive_request = ucp_tag_recv_nbx(worker, peer->receive_buffer,
                                            length, tag, UINT64_MAX, &parameters);
    if (UCS_PTR_IS_ERR(peer->receive_request)) {
        require_status(UCS_PTR_STATUS(peer->receive_request), "tag_recv", peer);
    }
    peer->receive_complete = (peer->receive_request == NULL);
}

static void post_send(test_peer_t *peer, size_t length, uint64_t tag)
{
    ucp_request_param_t parameters = {0};

    peer->send_request = ucp_tag_send_nbx(peer->endpoint, peer->send_buffer,
                                         length, tag, &parameters);
    if (UCS_PTR_IS_ERR(peer->send_request)) {
        require_status(UCS_PTR_STATUS(peer->send_request), "tag_send", peer);
    }
    peer->send_complete = (peer->send_request == NULL);
}

static void progress_request(test_peer_t *peer, void **request, int *complete,
                              const char *operation)
{
    ucs_status_t result;

    if (*complete) {
        return;
    }
    result = ucp_request_check_status(*request);
    if (result == UCS_INPROGRESS) {
        return;
    }
    ucp_request_free(*request);
    *request = NULL;
    *complete = 1;
    require_status(result, operation, peer);
}

static void wait_batch(void)
{
    double start_time;
    unsigned index;
    int all_complete;

    start_time = monotonic_seconds();
    for (;;) {
        check_endpoint_errors();
        all_complete = 1;
        for (index = 0; index < peer_count; ++index) {
            progress_request(&peers[index], &peers[index].send_request,
                              &peers[index].send_complete, "tag_send");
            progress_request(&peers[index], &peers[index].receive_request,
                              &peers[index].receive_complete, "tag_recv");
            if (!peers[index].send_complete || !peers[index].receive_complete) {
                all_complete = 0;
            }
        }
        if (all_complete) {
            return;
        }
        ucp_worker_progress(worker);
        if (monotonic_seconds() - start_time > REQUEST_TIMEOUT_SECONDS) {
            fail_message("batch deadline exceeded");
        }
    }
}

static void wait_single_request(void *request, const char *operation)
{
    ucs_status_t result;
    double start_time;

    check_endpoint_errors();
    if (UCS_PTR_IS_ERR(request)) {
        require_status(UCS_PTR_STATUS(request), operation, NULL);
    }
    if (request == NULL) {
        return;
    }
    start_time = monotonic_seconds();
    for (;;) {
        check_endpoint_errors();
        result = ucp_request_check_status(request);
        if (result != UCS_INPROGRESS) {
            break;
        }
        ucp_worker_progress(worker);
        if (monotonic_seconds() - start_time > REQUEST_TIMEOUT_SECONDS) {
            fail_message("flush/close deadline exceeded");
        }
    }
    ucp_request_free(request);
    require_status(result, operation, NULL);
}

static void flush_worker(void)
{
    ucp_request_param_t parameters = {0};

    /* Flush the shared iface too, including locally completed ACK operations. */
    wait_single_request(ucp_worker_flush_nbx(worker, &parameters), "worker_flush");
}

static void io_full(int fd, void *buffer, size_t length, int writing)
{
    unsigned char *position = buffer;
    ssize_t transferred;

    while (length != 0) {
        transferred = writing ? send(fd, position, length, MSG_NOSIGNAL) :
                                recv(fd, position, length, 0);
        if ((transferred < 0) && (errno == EINTR)) {
            continue;
        }
        if (transferred <= 0) {
            fail_message(writing ? "bootstrap send" : "bootstrap receive");
        }
        position += transferred;
        length -= (size_t)transferred;
    }
}

static void send_address(int fd, const ucp_address_t *address, size_t length)
{
    uint32_t wire_length;

    if ((length == 0) || (length > MAX_ADDRESS_BYTES)) {
        fail_message("worker address length");
    }
    wire_length = htonl((uint32_t)length);
    io_full(fd, &wire_length, sizeof(wire_length), 1);
    io_full(fd, (void *)address, length, 1);
}

static void *receive_address(int fd)
{
    uint32_t wire_length;
    size_t length;
    void *address;

    io_full(fd, &wire_length, sizeof(wire_length), 0);
    length = ntohl(wire_length);
    if ((length == 0) || (length > MAX_ADDRESS_BYTES)) {
        fail_message("peer worker address length");
    }
    address = malloc(length);
    if (address == NULL) {
        fail_message("allocate peer address");
    }
    io_full(fd, address, length, 0);
    return address;
}

static int open_bootstrap_socket(const test_options_t *options)
{
    struct addrinfo hints = {0};
    struct addrinfo *addresses;
    struct addrinfo *address;
    int fd = -1;
    int enabled = 1;

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = options->is_server ? AI_PASSIVE : 0;
    if (getaddrinfo(options->host, options->port, &hints, &addresses) != 0) {
        fail_message("getaddrinfo");
    }
    for (address = addresses; address != NULL; address = address->ai_next) {
        fd = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (fd < 0) {
            continue;
        }
        if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled)) != 0) {
            close(fd);
            fd = -1;
            continue;
        }
        if ((options->is_server &&
             (bind(fd, address->ai_addr, address->ai_addrlen) == 0)) ||
            (!options->is_server &&
             (connect(fd, address->ai_addr, address->ai_addrlen) == 0))) {
            break;
        }
        close(fd);
        fd = -1;
    }
    freeaddrinfo(addresses);
    if (fd < 0) {
        fail_message(options->is_server ? "bootstrap bind" : "bootstrap connect");
    }
    if (options->is_server && (listen(fd, MAX_CLIENTS) != 0)) {
        fail_message("bootstrap listen");
    }
    return fd;
}

static void set_socket_deadline(int fd)
{
    struct timeval deadline = {0};

    deadline.tv_sec = 30;
    if ((setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &deadline, sizeof(deadline)) != 0) ||
        (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &deadline, sizeof(deadline)) != 0)) {
        fail_message("bootstrap socket deadline");
    }
}

static void create_endpoint(test_peer_t *peer, int fd, void *address)
{
    ucp_ep_params_t parameters = {0};
    struct sockaddr_in socket_address = {0};
    socklen_t address_length;
    char peer_label[INET_ADDRSTRLEN];

    address_length = sizeof(socket_address);
    if ((getpeername(fd, (struct sockaddr *)&socket_address, &address_length) != 0) ||
        (socket_address.sin_family != AF_INET) ||
        (inet_ntop(AF_INET, &socket_address.sin_addr, peer_label,
                   sizeof(peer_label)) == NULL)) {
        fail_message("bootstrap peer label");
    }
    parameters.field_mask = UCP_EP_PARAM_FIELD_REMOTE_ADDRESS |
                             UCP_EP_PARAM_FIELD_ERR_HANDLER |
                             UCP_EP_PARAM_FIELD_PEER_HOST_ADDR;
    parameters.address = address;
    parameters.err_handler.cb = endpoint_error_callback;
    parameters.err_handler.arg = peer;
    parameters.peer_hostname = peer_label;
    parameters.vpid = (int)peer->peer_id;
    require_status(ucp_ep_create(worker, &parameters, &peer->endpoint),
                   "ep_create", peer);
}

static void bootstrap_peers(const test_options_t *options,
                             const ucp_address_t *address, size_t address_length)
{
    uint32_t hello[3];
    unsigned accepted_count;
    unsigned client_id;
    int socket_fd;
    int peer_fd;
    void *remote_address;

    socket_fd = open_bootstrap_socket(options);
    set_socket_deadline(socket_fd);
    if (options->is_server) {
        printf("READY MULTI bootstrap_port=%s clients=%u\n",
               options->port, options->clients);
        for (accepted_count = 0; accepted_count < options->clients; ++accepted_count) {
            peer_fd = accept(socket_fd, NULL, NULL);
            if (peer_fd < 0) {
                fail_message("bootstrap accept");
            }
            set_socket_deadline(peer_fd);
            io_full(peer_fd, hello, sizeof(hello), 0);
            client_id = ntohl(hello[1]);
            if ((ntohl(hello[0]) != BOOTSTRAP_MAGIC) ||
                (client_id == 0) || (client_id > options->clients) ||
                (ntohl(hello[2]) != options->iterations)) {
                fail_message("bootstrap role/iteration mismatch");
            }
            if (peers[client_id - 1].endpoint != NULL) {
                fail_message("duplicate client id");
            }
            remote_address = receive_address(peer_fd);
            send_address(peer_fd, address, address_length);
            create_endpoint(&peers[client_id - 1], peer_fd, remote_address);
            free(remote_address);
            close(peer_fd);
        }
        close(socket_fd);
    } else {
        set_socket_deadline(socket_fd);
        hello[0] = htonl(BOOTSTRAP_MAGIC);
        hello[1] = htonl(options->client_id);
        hello[2] = htonl(options->iterations);
        io_full(socket_fd, hello, sizeof(hello), 1);
        send_address(socket_fd, address, address_length);
        remote_address = receive_address(socket_fd);
        create_endpoint(&peers[0], socket_fd, remote_address);
        free(remote_address);
        close(socket_fd);
    }
    /* No bootstrap socket remains open during the TAG tests. */
}

static unsigned char *allocate_buffer(void)
{
    void *buffer = NULL;
    int result;

    result = posix_memalign(&buffer, BUFFER_ALIGNMENT, MAX_MESSAGE_BYTES);
    if (result != 0) {
        fail_message("allocate aligned data buffer");
    }
    memset(buffer, 0, MAX_MESSAGE_BYTES);
    return buffer;
}

static unsigned data_seed(unsigned client_id, unsigned size_index,
                           unsigned iteration, unsigned direction)
{
    return 31u + client_id * 61u + size_index * 19u +
           iteration * 7u + direction * 83u;
}

static unsigned char expected_byte(size_t offset, unsigned seed)
{
    return (unsigned char)((offset * 131u + (offset >> 8) + seed * 17u) & 255u);
}

static void fill_data(unsigned char *buffer, size_t length, unsigned seed)
{
    size_t offset;

    for (offset = 0; offset < length; ++offset) {
        buffer[offset] = expected_byte(offset, seed);
    }
}

static void check_receive_info(test_peer_t *peer, size_t length, uint64_t tag)
{
    require_status(peer->receive_status, "receive_callback", peer);
    if ((peer->receive_info.length != length) ||
        (peer->receive_info.sender_tag != tag)) {
        fprintf(stderr, "FAIL MULTI rank=%u peer=%u receive_length=%zu "
                        "expected_length=%zu sender_tag=%llu expected_tag=%llu\n",
                local_rank, peer->peer_id, peer->receive_info.length, length,
                (unsigned long long)peer->receive_info.sender_tag,
                (unsigned long long)tag);
        exit(EXIT_FAILURE);
    }
}

static void check_data(test_peer_t *peer, size_t length, unsigned seed)
{
    size_t offset;
    unsigned char expected;

    for (offset = 0; offset < length; ++offset) {
        expected = expected_byte(offset, seed);
        if (peer->receive_buffer[offset] != expected) {
            fprintf(stderr, "FAIL MULTI rank=%u peer=%u offset=%zu expected=%u actual=%u\n",
                    local_rank, peer->peer_id, offset, expected,
                    peer->receive_buffer[offset]);
            exit(EXIT_FAILURE);
        }
    }
    peer->verified_bytes += length;
}

static void exchange_tokens(unsigned kind, int sending)
{
    unsigned index;
    uint64_t token;
    uint64_t tag;

    reset_batch();
    for (index = 0; index < peer_count; ++index) {
        token = htobe64(peers[index].client_id);
        tag = make_tag(kind, peers[index].client_id, 0, 0);
        if (sending) {
            memcpy(peers[index].send_buffer, &token, sizeof(token));
            post_send(&peers[index], sizeof(token), tag);
        } else {
            post_receive(&peers[index], sizeof(token), tag);
        }
    }
    wait_batch();
    if (!sending) {
        for (index = 0; index < peer_count; ++index) {
            tag = make_tag(kind, peers[index].client_id, 0, 0);
            check_receive_info(&peers[index], sizeof(token), tag);
            memcpy(&token, peers[index].receive_buffer, sizeof(token));
            if (be64toh(token) != peers[index].client_id) {
                fail_message("control token mismatch");
            }
        }
    }
    flush_worker();
}

static void run_data_tests(const test_options_t *options)
{
    const size_t lengths[] = {64, 1024, 4000, 4008, 4096, 7259, 7260,
                              65536, 1048576, MAX_MESSAGE_BYTES};
    unsigned send_kind;
    unsigned receive_kind;
    unsigned size_index;
    unsigned iteration;
    unsigned index;
    unsigned seed;
    size_t length;
    uint64_t receive_tag;

    send_kind = options->is_server ? SERVER_DATA : CLIENT_DATA;
    receive_kind = options->is_server ? CLIENT_DATA : SERVER_DATA;
    for (size_index = 0; size_index < sizeof(lengths) / sizeof(lengths[0]); ++size_index) {
        length = lengths[size_index];
        for (iteration = 0; iteration < options->iterations; ++iteration) {
            reset_batch();
            /* Post every peer's receive before sending to any peer. */
            for (index = 0; index < peer_count; ++index) {
                seed = data_seed(peers[index].client_id, size_index, iteration, send_kind);
                fill_data(peers[index].send_buffer, length, seed);
                memset(peers[index].receive_buffer, 0xcc, length);
                receive_tag = make_tag(receive_kind, peers[index].client_id,
                                       size_index, iteration);
                post_receive(&peers[index], length, receive_tag);
            }
            for (index = 0; index < peer_count; ++index) {
                post_send(&peers[index], length,
                           make_tag(send_kind, peers[index].client_id, size_index, iteration));
            }
            /* A single progress loop services all active endpoints. */
            wait_batch();
            for (index = 0; index < peer_count; ++index) {
                receive_tag = make_tag(receive_kind, peers[index].client_id,
                                       size_index, iteration);
                check_receive_info(&peers[index], length, receive_tag);
                seed = data_seed(peers[index].client_id, size_index, iteration, receive_kind);
                check_data(&peers[index], length, seed);
            }
            flush_worker();
        }
        for (index = 0; index < peer_count; ++index) {
            printf("PASS MULTI TAG rank=%u peer=%u size=%zu iterations=%u\n",
                   local_rank, peers[index].peer_id, length, options->iterations);
        }
    }
}

int main(int argc, char **argv)
{
    test_options_t options;
    ucp_config_t *config;
    ucp_context_h context;
    ucp_params_t context_parameters = {0};
    ucp_worker_params_t worker_parameters = {0};
    ucp_request_param_t close_parameters = {0};
    ucp_address_t *address;
    size_t address_length;
    uint64_t verified_bytes = 0;
    unsigned index;

    setvbuf(stdout, NULL, _IONBF, 0);
    alarm(300);
    options = parse_options(argc, argv);
    local_rank = options.is_server ? 0 : options.client_id;
    peer_count = options.is_server ? options.clients : 1;
    peers = calloc(peer_count, sizeof(*peers));
    if (peers == NULL) {
        fail_message("allocate peer table");
    }
    for (index = 0; index < peer_count; ++index) {
        peers[index].client_id = options.is_server ? index + 1 : options.client_id;
        peers[index].peer_id = options.is_server ? index + 1 : 0;
        peers[index].endpoint_status = UCS_OK;
        peers[index].send_buffer = allocate_buffer();
        peers[index].receive_buffer = allocate_buffer();
    }
    require_status(ucp_config_read(NULL, NULL, &config), "config_read", NULL);
    context_parameters.field_mask = UCP_PARAM_FIELD_FEATURES;
    context_parameters.features = UCP_FEATURE_TAG;
    require_status(ucp_init(&context_parameters, config, &context), "ucp_init", NULL);
    ucp_config_release(config);
    worker_parameters.field_mask = UCP_WORKER_PARAM_FIELD_THREAD_MODE;
    worker_parameters.thread_mode = UCS_THREAD_MODE_SINGLE;
    require_status(ucp_worker_create(context, &worker_parameters, &worker), "worker_create", NULL);
    require_status(ucp_worker_get_address(worker, &address, &address_length),
                   "worker_address", NULL);
    bootstrap_peers(&options, address, address_length);
    ucp_worker_release_address(worker, address);
    printf("MULTI_SETUP rank=%u workers=1 endpoints=%u worker=%p\n",
           local_rank, peer_count, (void *)worker);

    exchange_tokens(START_TOKEN, options.is_server);
    run_data_tests(&options);
    exchange_tokens(DONE_TOKEN, !options.is_server);
    exchange_tokens(FINISH_TOKEN, options.is_server);
    for (index = 0; index < peer_count; ++index) {
        wait_single_request(ucp_ep_close_nbx(peers[index].endpoint, &close_parameters), "ep_close");
        peers[index].endpoint = NULL;
        verified_bytes += peers[index].verified_bytes;
    }
    flush_worker();
    ucp_worker_destroy(worker);
    ucp_cleanup(context);
    for (index = 0; index < peer_count; ++index) {
        free(peers[index].send_buffer);
        free(peers[index].receive_buffer);
    }
    free(peers);
    printf("PASS MULTI SUMMARY rank=%u peers=%u iterations=%u verified_bytes=%llu\n",
           local_rank, peer_count, options.iterations,
           (unsigned long long)verified_bytes);
    return 0;
}
