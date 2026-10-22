from pathlib import Path
import csv,json,math,zipfile
import numpy as np
import pandas as pd
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib import font_manager
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import SimpleDocTemplate,Paragraph,Spacer,Table,TableStyle,Image,PageBreak,KeepTogether
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib import colors
from reportlab.lib.enums import TA_LEFT

ROOT=Path(__file__).resolve().parent; D=ROOT/'data'; F=ROOT/'figures';F.mkdir(exist_ok=True)
font=str(ROOT/'assets/NotoSansSC-Regular.ttf')
font_manager.fontManager.addfont(font)
plt.rcParams.update({'font.family':font_manager.FontProperties(fname=font).get_name(),'font.size':10,'axes.spines.top':False,'axes.spines.right':False,'axes.unicode_minus':False,'svg.fonttype':'path','figure.facecolor':'white'})
RM='#087E8B';RC='#A3333D';GRAY='#576777';NAVY='#142D43'
lat=pd.read_csv(D/'latency.csv');bw=pd.read_csv(D/'bandwidth.csv');coll=pd.read_csv(D/'collective.csv');mem=pd.read_csv(D/'memory.csv');opt=pd.read_csv(D/'optimization64.csv')
for df,unit in [(lat,'us'),(bw,'MBps'),(coll,'us')]:
 for mode in ['rm','rc']:
  assert np.isfinite(df[f'{mode}_median_{unit}']).all()
  assert (df[f'{mode}_min_{unit}']<=df[f'{mode}_median_{unit}']).all()
  assert (df[f'{mode}_median_{unit}']<=df[f'{mode}_max_{unit}']).all()
assert len(lat)==len(bw)==24 and len(coll)==76 and len(mem)==8

def size(x):
 if x<1024:return f'{int(x)} B'
 if x<1048576:return f'{x/1024:g} KiB'
 return f'{x/1048576:g} MiB'
def ticks(ax,small=False):
 vals=[1,64,4096,262144,8388608] if not small else [4,64,1024,16384,262144,1048576]
 ax.set_xscale('log',base=2);ax.set_xticks(vals,[size(x) for x in vals]);ax.tick_params(axis='x',labelsize=8);ax.grid(True,which='major',alpha=.16);ax.set_xlabel('消息大小')
def series(ax,df,unit='us',log=True):
 for mode,color,label in [('rm',RM,'RM（发送池开启）'),('rc',RC,'RC')]:
  ax.plot(df.bytes,df[f'{mode}_median_{unit}'],color=color,marker='o',ms=3,lw=1.7,label=label)
  ax.fill_between(df.bytes,df[f'{mode}_min_{unit}'],df[f'{mode}_max_{unit}'],color=color,alpha=.15)
 if log:ax.set_yscale('log')
 ticks(ax,df.bytes.min()==4)
 ax.legend(frameon=False,fontsize=8)
def save(fig,name):
 fig.savefig(F/(name+'.png'),dpi=220,bbox_inches='tight');fig.savefig(F/(name+'.svg'),bbox_inches='tight');plt.close(fig)

fig,axs=plt.subplots(1,2,figsize=(11,4.2));series(axs[0],lat);axs[0].set_ylabel('OSU 时延（μs，对数轴，越低越好）');axs[0].set_title('完整范围：1 B–8 MiB')
small=lat[lat.bytes<=131072];series(axs[1],small,log=False);axs[1].set_xlim(.8,160000);axs[1].set_xticks([1,64,4096,131072],['1 B','64 B','4 KiB','128 KiB']);axs[1].set_title('小／中消息：线性纵轴');axs[1].set_ylabel('时延（μs）')
fig.suptitle('点对点时延：优化后接近 RC，但当前测量点仍高于 RC',fontsize=13,color=NAVY);fig.tight_layout();save(fig,'01_latency')
fig,ax=plt.subplots(figsize=(10.8,4.5));series(ax,bw,'MBps');ax.set_ylabel('带宽（MB/s，对数轴，越高越好）');ax.set_title('窗口64带宽：保留协议切换附近的突变，路径归因待核实',color=NAVY);fig.tight_layout();save(fig,'02_bandwidth')
fig,axs=plt.subplots(2,2,figsize=(11,7));
for ax,(test,df) in zip(axs.flat,coll.groupby('test',sort=False)):
 series(ax,df);ax.set_title(test);ax.set_ylabel('时延（μs，对数轴）')
fig.suptitle('4进程集合通信：每节点2进程；同节点通信也限定 URMA 路径',fontsize=13,color=NAVY);fig.tight_layout(rect=[0,0,1,.96]);save(fig,'03_collectives')
fig,axs=plt.subplots(1,2,figsize=(11,4.2))
for mode,color,label in [('rm',RM,'RM（发送池开启）'),('rc',RC,'RC')]:
 df=mem[mem['mode']==mode];x=df.peers.to_numpy(); y=df.private_median_KiB.to_numpy()/1024
 axs[0].errorbar(x,y,yerr=[y-df.private_min_KiB.to_numpy()/1024,df.private_max_KiB.to_numpy()/1024-y],color=color,marker='o',capsize=4,label=label)
 axs[1].plot(x,df.jetty_r1,color=color,marker='o',label=label)
for ax in axs:ax.set_xticks([1,2,4,8]);ax.set_xlabel('中心进程的远端 MPI 对端数');ax.set_ylim(bottom=0);ax.grid(alpha=.16);ax.legend(frameon=False,fontsize=8)
axs[0].set_ylabel('私有内存增量（MiB）');axs[0].set_title('初始化前→交换后：RM固定开销仍高')
axs[1].set_ylabel('中心进程本地 Jetty 数量');axs[1].set_title('本地资源共享：RM=1；RC随对端数增长')
fig.suptitle('内存与资源：三轮取样；64 B顺序星型通信，最多9进程',fontsize=13,color=NAVY);fig.tight_layout();save(fig,'04_memory_resources')
fig,axs=plt.subplots(1,2,figsize=(11,4.2));labels=['RM 发送池关闭','RM 发送池开启','RC'];vals=opt.median_us.to_numpy()
axs[0].bar(labels,vals,color=[GRAY,RM,RC],width=.55);axs[0].set_yscale('log');axs[0].set_ylim(1,2200);axs[0].set_ylabel('64 B时延（μs，对数轴）');axs[0].set_title('同一候选库，仅切换发送池开关')
for i,v in enumerate(vals):axs[0].text(i,v*1.15,f'{v:.2f}',ha='center',fontsize=11)
cat=['DATA 注册','DATA 注销','ACK 注册','ACK 注销','DATA 提交','ACK 提交'];v=[(423211.82+459482.19)/2042,(194701.73+203063.93)/2042,(433638.07+457592.33)/2042,(206583.62+209413.38)/2042,(273.99+277.1)/2042,(294.68+332.07)/2042]
axs[1].barh(cat[::-1],v[::-1],color=[RM]*4+[GRAY]*2 if False else [GRAY,GRAY,RM,RM,RM,RM]);axs[1].set_xlabel('未开启发送池：每次调用平均耗时（μs）');axs[1].set_xlim(0,550)
for i,z in enumerate(v[::-1]):axs[1].text(z+5,i,f'{z:.2f}',va='center',fontsize=9)
axs[1].set_title('注册／注销开销来自分阶段计时')
fig.suptitle('性能根因与优化：932.43→2.94 μs，约317倍改善',fontsize=13,color=NAVY);fig.tight_layout();save(fig,'05_optimization')

pdfmetrics.registerFont(TTFont('CN',font));pdfmetrics.registerFont(TTFont('TimesCustom','/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf'))
styles={
 'title':ParagraphStyle('title',fontName='CN',fontSize=23,leading=32,textColor=colors.HexColor(NAVY),spaceAfter=14),
 'h':ParagraphStyle('h',fontName='CN',fontSize=16,leading=24,textColor=colors.HexColor(NAVY),spaceAfter=10),
 'sub':ParagraphStyle('sub',fontName='CN',fontSize=11,leading=17,textColor=colors.HexColor(RM),spaceAfter=8),
 'body':ParagraphStyle('body',fontName='CN',fontSize=10,leading=16,spaceAfter=8,wordWrap='CJK'),
 'note':ParagraphStyle('note',fontName='CN',fontSize=8,leading=12,textColor=colors.HexColor(GRAY),spaceAfter=7,wordWrap='CJK')}
S=[]
def P(text,kind='body'):return Paragraph(text,styles[kind])
def add(text,kind='body'):S.append(P(text,kind))
def table(data,widths=None,fs=8):
 arr=[[P(str(c),'note') if isinstance(c,str) and len(c)>38 else str(c) for c in row] for row in data]
 t=Table(arr,colWidths=widths,repeatRows=1,hAlign='LEFT');t.setStyle(TableStyle([('FONTNAME',(0,0),(-1,-1),'CN'),('FONTSIZE',(0,0),(-1,-1),fs),('LEADING',(0,0),(-1,-1),fs+4),('BACKGROUND',(0,0),(-1,0),colors.HexColor(NAVY)),('TEXTCOLOR',(0,0),(-1,0),colors.white),('ROWBACKGROUNDS',(0,1),(-1,-1),[colors.HexColor('#F0F5F7'),colors.white]),('VALIGN',(0,0),(-1,-1),'TOP'),('LEFTPADDING',(0,0),(-1,-1),6),('RIGHTPADDING',(0,0),(-1,-1),6),('TOPPADDING',(0,0),(-1,-1),6),('BOTTOMPADDING',(0,0),(-1,-1),6),('LINEBELOW',(0,0),(-1,0),.5,colors.HexColor(NAVY))]));S.append(t);S.append(Spacer(1,10))
def img(name,w=510):
 from PIL import Image as PIL
 p=F/(name+'.png');im=PIL.open(p);S.append(Image(str(p),width=w,height=w*im.height/im.width));S.append(Spacer(1,8))
def page(title):
 if S:S.append(PageBreak())
 add(title,'h')

add('RM+CTP：MPI四维对照\n与时延优化验证'.replace('\n','<br/>'),'title');add('阶段汇报｜2026年10月20日｜供祝佳博士评审','sub')
add('已完成双节点 RM/RC 对照，覆盖点对点时延、带宽、四项集合通信时延，以及1／2／4／8对端的中心进程内存和资源数量。当前结论适用于本报告中的版本、配置与负载。')
table([['维度','主要结果','结论'],['小消息优化','64 B：932.43 → 2.94 μs','逐包注册／注销为主要瓶颈；预注册复用有效'],['点对点时延','优化后64 B：RM 2.91，RC 2.32 μs','当前24个消息大小RM时延均高于RC'],['带宽','8 MiB：RM 4684.41，RC 6331.51 MB/s','大包RC更高；中间区间存在明显路径突变'],['集合通信','4项、4进程、各3轮全部完成','分段特征明显，不能概括为某模式全面占优'],['内存／资源','RM约84.5 MiB；RC约45.9–47.9 MiB','未体现总内存优势；RM中心Jetty固定为1']],[82,205,223])
add('可直接汇报的表述','sub');add('已跑通 RM+CTP 在 MPI 下的基础能力和代表性集合通信，定位并修复了大消息切换时的远端段缓存问题。通过预注册发送／ACK缓冲池，将64 B时延从约932 μs降至约2.94 μs。资源对照确认中心RM Jetty不随1～8对端增长，但接收侧固定内存开销仍高；下一步重点优化接收缓冲注册，并核实协议切换附近的性能突变。')
add('能力边界：实测最多中心8个远端MPI进程，共9进程、2个物理节点；未测最大对端上限。ACK丢弃后的通信完成已验证，但ACK丢弃专门触发重传尚未覆盖。','note')

page('01｜点对点时延');img('01_latency');add('测量口径：osu_latency v7.5.2，2进程、每节点1进程，绑定core0；预热100次、计时1000次，RM/RC交错运行3轮。图线为每轮输出平均时延的三轮中位数，阴影为三轮最小值到最大值。OSU值为往返时间的一半，不是跨节点时钟测得的单程时间。')
table([['大小','RM（μs）','RC（μs）','RM相对RC'],*[ [size(s),f'{lat.loc[lat.bytes==s,"rm_median_us"].iloc[0]:.2f}',f'{lat.loc[lat.bytes==s,"rc_median_us"].iloc[0]:.2f}',f'{(lat.loc[lat.bytes==s,"rm_median_us"].iloc[0]/lat.loc[lat.bytes==s,"rc_median_us"].iloc[0]-1)*100:.1f}% 高'] for s in [64,4096,131072,262144,8388608]]],[100,120,120,170])
add('观察：1 B～128 KiB范围，RM时延逐步增长；256 KiB处，两种模式均跳至约1 ms。完整范围RM仍高于RC，8 MiB差约4.3%。跳变与协议选择相关的可能性需要日志确认；本报告不将阈值位置直接解释为硬件限制。')

page('02｜窗口64带宽');img('02_bandwidth');add('测量口径：osu_bw v7.5.2，2进程；窗口64，预热5次、计时20次，3轮交错运行，关闭校验及诊断。MB/s采用OSU输出单位，不等同于MiB/s。')
table([['消息大小','RM（MB/s）','RC（MB/s）'],*[ [size(s),f'{bw.loc[bw.bytes==s,"rm_median_MBps"].iloc[0]:.2f}',f'{bw.loc[bw.bytes==s,"rc_median_MBps"].iloc[0]:.2f}'] for s in [64,4096,8192,65536,131072,8388608]]],[170,170,170])
add('观察：RC在4 KiB为7513.12 MB/s，到8 KiB降至7.72 MB/s；RM在64 KiB为4582.66 MB/s，到128 KiB降至88.01 MB/s。该现象三轮复现。8～64 KiB的RM观测值明显更高，但不能脱离不同消息路径，将它表述为RM模式的固有优势。8 MiB时RC比RM高约35.2%（或RM比RC低约26.0%）。')

page('03｜四项集合通信时延');img('03_collectives');add('测量口径：4进程，每节点2进程，绑定core0／core1。消息4 B～1 MiB，预热20次、计时100次，3轮交错运行。仅允许URMA与self，同节点进程也未启用共享内存传输。集合通信数值采用OSU输出，不能与点对点半往返时延直接作同一指标比较。')
add('观察：小消息各项RC中位数均更低；Bcast、Alltoall在RC的8 KiB处发生明显跳变，大消息部分点位RM较低。Allreduce、Gatherv的部分RC重复结果波动较大，完整最小／最大值保留在附表，不能只选个别点宣称全面胜出。','note')

page('04｜内存开销与资源共享');img('04_memory_resources');add('测量口径：64 B顺序星型通信，中心进程在节点89，1／2／4／8个远端进程在节点97；每种规模每种模式3次。队列深度RX=1024、TX=256；RM优先级请求5，未测硬件优先级。私有内存增量=交换后Private_Clean+Private_Dirty减初始化前对应值，单位KiB；图中换算MiB。该值不是设备内存、总系统内存或所有进程合计。')
table([['对端数','RM私有增量 MiB','RC私有增量 MiB','RM Jetty','RC Jetty'],*[ [str(n),f'{mem[(mem["mode"]=="rm")&(mem.peers==n)].private_median_KiB.iloc[0]/1024:.2f}',f'{mem[(mem["mode"]=="rc")&(mem.peers==n)].private_median_KiB.iloc[0]/1024:.2f}','1',str(n+2)] for n in [1,2,4,8]]],[70,130,130,90,90])
add('结论：中心RM本地Jetty固定为1，RC从3增至10，直接体现本地队列资源共享；但RM远端target_jetty数量仍为3／5／9／17，不能说全部资源恒定。当前RM总私有增量约84.5 MiB，高于RC；两者在该规模内都基本持平，不能外推内存交叉点或最大连接容量。RC部分重复存在约2 MiB变化，应保留波动范围。')

page('05｜时延根因及预注册缓冲池');img('05_optimization');add('原路径：每个DATA和软件ACK都分配缓冲区、注册段，完成后注销并释放。诊断计时在资源销毁前输出，排除退出时1024个接收缓冲区的注销；仍含少量建链／MPI控制消息及预热，未将其称为纯计时区间。')
add('证据链：100次与1000次测试，每节点DATA和ACK计数各增加900；注册每次约415～450 μs，注销约191～205 μs，而发送提交仅约0.3 μs。新版本初始化时将256×4 KiB发送池注册为一整段，DATA／ACK按槽位复用，逐操作注册／注销计时归零。')
add('控制变量对照：同一候选库，仅切换TX_POOL=n/y；64 B、预热100次、计时1000次、关闭校验及诊断，三轮中位数932.43→2.94 μs，约317.15倍改善，时延降低99.68%。此实验的RC为2.36 μs；完整曲线单独取样得到64 B RM2.91／RC2.32 μs，两组实验不混作同一轮数据。')
add('归因边界：无需修改URMA SDK、provider、驱动或固件即可消除主要差距，说明主要问题是适配层在逐消息路径中反复调用高成本注册接口。不能由此断言SDK存在缺陷，也不能把post到CQE的重叠时间当作纯硬件链路时延。')

page('06｜实现分工、验证范围与后续');table([['层次','已有能力／本项目工作'],['URMA、provider、驱动、设备','提供RM模式、CTP选择、Jetty/JFR/JFC资源、段注册、SEND/READ/WRITE提交与完成队列'],['HUCX RM适配层','接入UCT操作；管理端点与缓冲生命周期；软件DATA/ACK、序号、重复抑制、超时重传；此次增加预注册发送池'],['UCP与MPI','沿用上层消息协议、大小消息选择、分片及集合通信算法']],[130,380])
add('已完成的验证','sub');add('缓冲池开启后：点对点1 B～8 MiB时延、窗口64带宽各24点数据校验通过；4进程四项集合通信各19点通过。DATA软件丢弃后，两节点同一操作ID约100 ms后重新发送，最终校验通过、live_ops=0。ACK丢弃1个／8个后通信完成，未观察到重传；代码使用累计ACK，后续ACK可覆盖早期丢失。')
add('尚未完成／不应宣称','sub');add('未做物理链路丢包；未完整验证ACK专项触发重传、重传耗尽下缓冲池版fail-fast、所有资源／段泄漏回归、长时间高并发压力及最大对端上限。未测硬件TP占用，不能把软件Jetty计数等同于TP容量。大消息可走原生READ／WRITE，不能将所有大消息都称为4 KiB软件分片重传。')
add('建议后续顺序','sub');add('1. 优化接收缓冲注册：当前1024个接收缓冲逐个注册，保留其正确性／队列生命周期约束，先做独立内存对照。<br/>2. 核实RC 8 KiB及双方大消息突变：记录实际UCP路径、段注册／导入频率，区分适配层与底层调用成本。<br/>3. 补受控ACK丢弃、并发及资源平衡回归，再扩16／32／64对端，分别测建链容量和并发通信能力。<br/>4. 保留此次版本作为可复现阶段基线；缓冲池目前为显式开启的实验功能。')
add('本报告数据均来自用户在节点执行后回传的终端汇总。已作行数、范围及数值一致性检查，但未收到原始节点文件，未宣称对节点CSV逐字节校验。未生成或冒充真实终端截图。测试前后vmstat间隔采样接近100%空闲，但不是全程负载监控。','note')

page('附表A｜完整点对点时延数据');add('单位μs；每项为三轮中位数，括号内为三轮最小值–最大值。','note')
def fulltable(df,unit):
 rows=[['消息大小','RM 中位数（范围）','RC 中位数（范围）']]
 for _,r in df.iterrows():rows.append([size(r.bytes),f'{r[f"rm_median_{unit}"]:.2f} ({r[f"rm_min_{unit}"]:.2f}–{r[f"rm_max_{unit}"]:.2f})',f'{r[f"rc_median_{unit}"]:.2f} ({r[f"rc_min_{unit}"]:.2f}–{r[f"rc_max_{unit}"]:.2f})'])
 table(rows,[90,210,210],8)
fulltable(lat,'us')
page('附表B｜完整窗口64带宽数据');add('单位MB/s；每项为三轮中位数，括号内为最小值–最大值。','note');fulltable(bw,'MBps')
for test,df in coll.groupby('test',sort=False):
 page('附表C｜'+test+'，4进程');add('单位μs；三轮中位数（最小值–最大值）。','note');fulltable(df,'us')
page('附表D｜内存、资源与可复现条件');table([['模式','对端','私有增量KiB中位数','范围KiB','Jetty（三轮）'],*[ [r['mode'],str(r.peers),str(r.private_median_KiB),f'{r.private_min_KiB}–{r.private_max_KiB}',f'{r.jetty_r1}/{r.jetty_r2}/{r.jetty_r3}'] for _,r in mem.iterrows()]],[65,50,150,150,95])
add('版本与参数','sub');add('OSU Micro-Benchmarks 7.5.2；HUCX运行时报告1.15.0；ARM64双节点，每节点192核。使用同一HMPI及候选UCX库；候选源码7cc4b6559bfe52539b48da31154e3e27bff1898e，URMA模块SHA256：cbd3a114b90ec727f67c196c37d30da3567439821df2c34c89666fc0c8182f56。')
add('共同配置：UCX_PROTO_ENABLE=n，设备udmac0d1e2:1，性能测试沿用自动协议阈值；RM路径rm_urma,self，RC路径rc_urma,um_urma:aux,self。RM RX1024/TX256、优先级请求5、超时100 ms、重试8。RC/UM RX1024/TX256/TX_JFC_LEN256。性能运行无资源／段观察器；内存运行有资源观察器、强制SEND（RNDV_THRESH=inf）、不绑核。因此四个维度是各自明确口径的对照，不能当作完全相同负载下的单一测量。')
add('节点结果目录索引','sub');table([['维度','目录末级名称'],['时延','osu_pool_latency_compare_gsdfx3hc'],['带宽','osu_pool_bw_compare_8s07e2bh'],['集合通信','osu_pool_collective_compare_4cot1zs1'],['内存／资源','memory_pool_scale_repeats_6tmxezy5'],['64 B开关对照','osu_tx_pool_perf64_e5ifqhlc']],[130,380])
add('字体：当前环境使用Noto Sans SC作为中文无衬线字体；可编辑SVG与CSV随附件提供。所有图使用相同颜色映射：青色RM、暗红RC；阴影／误差线为三轮范围，不是置信区间。','note')

def footer(c,doc):
 c.setStrokeColor(colors.HexColor('#D5DFE5'));c.line(42,36,553,36);c.setFont('CN',7);c.setFillColor(colors.HexColor(GRAY));c.drawString(42,24,'RM/RC阶段对照 · 2026-10-20 · 数据来源：节点终端回传');c.drawRightString(553,24,str(doc.page))
pdf=ROOT/'RM_RC_四维对照与优化报告_20261020.pdf'
doc=SimpleDocTemplate(str(pdf),pagesize=(595.28,841.89),leftMargin=42,rightMargin=42,topMargin=38,bottomMargin=46,title='RM+CTP MPI四维对照与优化验证',author='云开')
doc.build(S,onFirstPage=footer,onLaterPages=footer)
readme='''# RM/RC四维对照与优化验证（2026-10-20）
数据来源：本会话节点终端回传；未获得节点原始文件，不声称逐字节哈希核验。
figures：独立PNG与矢量SVG；data：完整CSV；PDF：详细说明和全量数据表。
中位数来自3轮，每轮值为OSU输出，范围为三轮最小/最大，不是置信区间。
最重要结论：同候选库64B预注册发送池开关对照932.43→2.94us；中心Jetty 1–8对端RM固定1、RC3/4/6/10；RM私有增量84.5MiB仍高于RC45.9–47.9MiB。
不应宣称RM小包一定更快、全部资源恒定、最大支持8对端或已验证物理丢包。
'''
(ROOT/'README.md').write_text(readme)
print(pdf)
