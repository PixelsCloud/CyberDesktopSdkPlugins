"""Generate the fixed footprints, bilingual settings and code-native design board."""
import json
from pathlib import Path
from html import escape

ROOT = Path(__file__).resolve().parents[1]
DESIGN = ROOT.parents[1] / 'design' / 'productivity'
families = [
    ('timer', '番茄钟', 'Focus timer', [(1, 2), (2, 2)], '#203a37', '#83e2b6'),
    ('todo', '待办清单', 'Tasks', [(1, 4), (2, 4)], '#eef3f9', '#4785cf'),
    ('countdown', '倒数日', 'Countdown', [(1, 2), (1, 4)], '#efe8dd', '#cb704e'),
    ('network', '网速仪表', 'Network', [(1, 2), (1, 4)], '#162735', '#66dbe6'),
    ('disk', '磁盘空间', 'Storage', [(1, 2), (2, 2)], '#273441', '#8dbef0'),
    ('notes', '便签', 'Notes', [(1, 4), (2, 4)], '#fff0bb', '#ad7834'),
    ('rss', 'RSS 资讯', 'RSS reader', [(1, 4), (2, 4)], '#f4f0e8', '#de8640'),
]

def field(id, zh, en, typ, default, section='内容', en_section='Content', **kw):
    value = f' fields {{ id: "{id}" label: {json.dumps(zh,ensure_ascii=False)} type: {typ} section: {json.dumps(section,ensure_ascii=False)}'
    prop = {'INTEGER':'integer_default','NUMBER':'number_default','TEXT':'text_default','FONT':'text_default','CHOICE':'text_default','TOGGLE':'bool_default'}[typ]
    value += f' {prop}: {json.dumps(default,ensure_ascii=False)}'
    for k,v in kw.items():
        value += f' {k}: {v}'
    value += f' localized {{ key: "en" value {{ label: {json.dumps(en)} section: {json.dumps(en_section)} }} }}'
    if typ == 'CHOICE':
        choices = [('focus','番茄钟','Pomodoro'),('countdown','倒计时','Countdown')] if id=='mode' else [('zh-CN','简体中文','简体中文'),('en','English','English')]
        for v,c,e in choices:value+=f' choices {{ value: "{v}" label: "{c}" localized_labels {{ key: "en" value: "{e}" }} }}'
    return value+' }\n'

settings = {
    'timer': field('mode','计时模式','Timer mode','CHOICE','focus')+field('focus_minutes','专注时长（分钟）','Focus minutes','INTEGER',25,minimum=1,maximum=180,step=1)+field('break_minutes','休息时长（分钟）','Break minutes','INTEGER',5,minimum=1,maximum=60,step=1)+field('auto_break','自动开始休息','Automatically start break','TOGGLE',False),
    'todo': field('tasks','任务（每行一项，最多 20 项）','Tasks (one per line, up to 20)','TEXT','整理桌面\n完成今天的计划\n阅读十分钟',max_length=2048,text_editor='MULTILINE'),
    'countdown': field('title','事件名称','Event title','TEXT','下个假期',max_length=128)+field('date','目标日期（YYYY-MM-DD）','Target date (YYYY-MM-DD)','TEXT','2027-01-01',max_length=10)+field('annual','每年重复','Repeat annually','TOGGLE',False),
    'network': field('history','显示最近 60 秒曲线','Show 60-second history','TOGGLE',True),
    'disk': field('drive','盘符（如 C:\\）','Drive root (e.g. C:\\)','TEXT','C:\\',max_length=3)+field('refresh_seconds','采样间隔（秒）','Sample interval (seconds)','INTEGER',30,minimum=10,maximum=300,step=10),
    'notes': field('text','便签内容（每行可点击复制）','Note text (click a line to copy)','TEXT','记录灵感，保持专注\n今天也要把事情做好',max_length=2048,text_editor='MULTILINE')+field('copy_all','点击复制整张便签','Copy entire note on click','TOGGLE',True),
    'rss': field('url','订阅源 URL（RSS / Atom）','Feed URL (RSS / Atom)','TEXT','https://www.qt.io/blog/rss.xml',max_length=2048)+field('refresh_minutes','刷新间隔（分钟）','Refresh interval (minutes)','INTEGER',15,minimum=5,maximum=180,step=5)+field('rotation_seconds','标题轮播间隔（秒）','Headline rotation (seconds)','INTEGER',10,minimum=5,maximum=60,step=1),
}

def text(x,y,value,size=12,color='#203039',anchor='start'):
    return f'<text x="{x}" y="{y}" font-size="{size}" fill="{color}" text-anchor="{anchor}">{escape(value)}</text>'

def card(kind,w,h,bg,accent):
    dark=kind in ('timer','network','disk'); ink='#eff9f8' if dark else '#263039'; muted='#a0b6bf' if dark else '#77766f'
    s=f'<rect x="1" y="1" width="{w-4}" height="{h-5}" rx="5" fill="{bg}" filter="url(#shadow)"/>'
    if kind=='timer':
        if h>120:
            s+=f'<circle cx="{w/2-2}" cy="{h*.45}" r="{w*.34}" stroke="#405b55" stroke-width="5" fill="none"/><circle cx="{w/2-2}" cy="{h*.45}" r="{w*.34}" stroke="{accent}" stroke-width="5" fill="none" stroke-dasharray="190 140" transform="rotate(-90 {w/2-2} {h*.45})"/>'
            s+=text(w/2,h*.46,'18:42',27,ink,'middle')+text(w/2,h*.59,'专注中',11,muted,'middle')
        else:s+=text(12,24,'专注中',11,accent)+text(w/2,60,'18:42',31,ink,'middle')
        s+=f'<rect x="12" y="{h-32}" width="{w-28}" height="18" rx="9" fill="#405b55"/>'+text(w/2,h-19,'暂停  ·  重置',10,ink,'middle')
    elif kind=='todo':
        s+=text(12,24,'今日待办',12,ink)+text(w-18,24,'1 / 3',11,accent,'end')
        count=3 if h>120 else 1
        for i,line in enumerate(['整理桌面','完成今天的计划','阅读十分钟'][:count]):
            y=40+i*(h-60)/count
            s+=f'<rect x="13" y="{y}" width="14" height="14" rx="4" fill="{accent if i==0 else "#dde6f0"}"/>'+text(36,y+13,line,15,muted if i==0 else ink)
            s+=f'<path d="M 12 {y+25} H {w-18}" stroke="#dce3ea"/>'
        if h<120:s+=text(12,h-17,'下一项  完成今天的计划',10,muted)
    elif kind=='countdown':
        s+=text(12,24,'下个假期',11,muted)
        numx=w*.36 if w>200 else w/2
        s+=f'<rect x="{numx-45}" y="31" width="90" height="{h-46}" rx="5" fill="#faf8f3"/>'+text(numx,h-25,'080',32,ink,'middle')+f'<path d="M {numx-45} {h*.59} h 90" stroke="#e8e2d7"/>'
        if w>200:s+=text(w*.64,54,'天后',17,accent)+text(w*.64,75,'2027-01-01',11,muted)
        else:s+=text(w-15,24,'天',10,accent,'end')
    elif kind=='network':
        s+=text(12,24,'实时网速',11,muted)
        for i,(label,v,col) in enumerate([('↓','12.8 MB/s',accent),('↑','1.24 MB/s','#b2a3ef')]):
            x=12+(w*.5 if w>200 else 0)*i;y=49+(23 if w<200 else 0)*i
            s+=text(x,y,label,13,col)+text(x+17,y,v,18 if w>200 else 16,ink)
        if w>200:s+=f'<path d="M12 79 l20 -2 18 3 18 -12 20 7 18 -20 18 14 20 -9 20 8 18 -16 18 11 20 -5 18 8 20 -12 10 3" stroke="{accent}" fill="none"/>'
    elif kind=='disk':
        s+=text(12,24,'C:  本地磁盘',11,muted)
        if h>120:
            s+=f'<circle cx="{w/2}" cy="{h*.47}" r="{w*.3}" stroke="#485765" stroke-width="6" fill="none"/><circle cx="{w/2}" cy="{h*.47}" r="{w*.3}" stroke="{accent}" stroke-width="6" fill="none" stroke-dasharray="190 110" transform="rotate(-90 {w/2} {h*.47})"/>'+text(w/2,h*.48,'62%',29,ink,'middle')+text(w/2,h*.59,'已用',11,muted,'middle')
            s+=text(w/2,h-30,'剩余 182 GB',15,ink,'middle')+text(w/2,h-14,'总计 476 GB',10,muted,'middle')
        else:s+=text(12,54,'182 GB',26,ink)+text(12,72,'剩余 · 总计 476 GB',10,muted)+f'<rect x="12" y="{h-19}" width="{w-28}" height="4" rx="2" fill="#485765"/><rect x="12" y="{h-19}" width="{(w-28)*.62}" height="4" rx="2" fill="{accent}"/>'
    elif kind=='notes':
        s+=f'<rect x="{w*.35}" y="0" width="{w*.27}" height="8" rx="2" fill="#d1b97b" opacity=".5"/>'+text(12,24,'随手记',11,accent)
        for i,line in enumerate(['记录灵感，保持专注','今天也要把事情做好']):
            y=49+i*26
            s+=text(12,y,line,15,ink)+f'<path d="M 12 {y+6} H {w-18}" stroke="#e2ce94"/>'
        if h>120:s+=text(12,h-18,'点击复制',10,accent)
    elif kind=='rss':
        s+=f'<rect x="12" y="12" width="25" height="18" rx="4" fill="{accent}"/>'+text(24,25,'RSS',9,'#fff','middle')+text(45,25,'Qt Blog',11,muted)
        for i,line in enumerate(['新的工具，让桌面工作更轻松','设计与性能：本周更新'][:2 if h>120 else 1]):
            y=52+i*60
            s+=text(12,y,line,15,ink)+text(12,y+20,'10-13 · 点击阅读全文',10,muted)+f'<path d="M12 {y+29} H{w-18}" stroke="#ded8ce"/>'
        if h>120:s+=text(12,h-17,'后台更新 · 保留最近标题',10,accent)
    return s

def main():
    DESIGN.mkdir(parents=True,exist_ok=True);catalog=[];parts=[]
    for idx,(kind,zh,en,sizes,bg,accent) in enumerate(families):
        top=90+idx*255
        parts.append(text(28,top,zh,21,'#eef3f6')+text(28,top+23,en,11,'#9bb0bb'))
        left=180
        for rows,cols in sizes:
            size=f'{rows}x{cols}';id=f'com.cyberdesktop.{kind}.{size}';folder=f'{kind}-{size}'
            path=ROOT/'packages'/folder;path.mkdir(parents=True,exist_ok=True)
            proto=f'id: "{id}"\nname: "{zh} {cols}×{rows}"\nversion: "1.0.0"\nauthor: "CyberDesktop"\nabi_version: 1\nentry: "plugin.dll"\npreview: "preview.png"\ncontent_padding: 4\nicon: "assets/icons/{kind}.png"\ndescription: "{en}; independent settings and partial content updates."\n'
            if kind in ('network', 'disk'):proto+='capabilities: SYSTEM_INFO\n'
            if kind=='rss':proto+='capabilities: CONTENT\nrequired_services { id: "content.feeds" version: 1 }\n'
            proto+=f'footprint_rules {{ topology: RECT min_cells: {rows*cols} max_cells: {rows*cols} max_columns: {cols} max_rows: {rows} }}\nlayouts {{ id: "{size}" name: "{cols}×{rows}（宽×高）" topology: RECT is_default: true allowed_rotations: 0\n'
            for r in range(rows):
                for c in range(cols):proto+=f' cells {{ a: {c} b: {r} }}\n'
            proto+='}\nsettings_page {\n'+f' title: "{zh}设置" description: "单击控件操作；拖动移位不触发点击。每块砖独立保存配置。"\n localized {{ key: "en" value {{ title: "{en} settings" description: "Click controls to interact. Dragging does not activate a click. Settings are saved per instance." }} }}\n'
            proto+=settings[kind]+field('background_corner_radius','背景圆角（px）','Background corner radius (px)','INTEGER',5,section='背景',en_section='Background',minimum=0,maximum=32,step=1)+field('font_family','字体','Font','FONT','@siyuan-heavy',max_length=128)+field('language','语言','Language','CHOICE','zh-CN')+'}\n'
            (path/'plugin.textproto').write_text(proto,encoding='utf-8')
            catalog.append(dict(id=id,folder=folder,kind=idx,rows=rows,columns=cols,name=f'{zh} {cols}×{rows}'))
            w=cols*77;h=rows*99
            parts.append(text(left,top-5,f'{cols} × {rows}  /  {w} × {h} px',10,'#9bb0bb'))
            parts.append(f'<g transform="translate({left} {top+10})">'+card(kind,w,h,bg,accent)+'</g>')
            left+=w+55
    (ROOT/'catalog.json').write_text(json.dumps(catalog,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    svg='<svg xmlns="http://www.w3.org/2000/svg" width="1040" height="1870" viewBox="0 0 1040 1870"><defs><filter id="shadow" x="-10%" y="-10%" width="130%" height="140%"><feDropShadow dx="0" dy="3" stdDeviation="2" flood-color="#000" flood-opacity=".2"/></filter></defs><rect width="1040" height="1870" fill="#14202a"/><g font-family="Gen Shin Gothic Heavy,Microsoft YaHei,Segoe UI,sans-serif">'+text(28,37,'桌面效率插件 · 14 个布局',25,'#eef3f6')+''.join(parts)+'</g></svg>'
    (DESIGN/'concept.svg').write_text(svg,encoding='utf-8')
    (DESIGN/'index.html').write_text('<!doctype html><html lang="zh-CN"><meta charset="utf-8"><title>桌面效率插件设计</title><style>body{margin:0;background:#14202a;color:#dce6ec;font:16px system-ui}main{max-width:1040px;margin:auto}img{width:100%}p{padding:12px 28px}</style><main><p>左侧为紧凑版，右侧为扩展版。设计以长方形小格子为基准；交互只更新当前插件。</p><img src="concept.svg" alt="七类插件十四个布局设计"><p><a style="color:#83e2b6" href="rendered.html">查看实际 C++ 渲染结果</a></p></main></html>',encoding='utf-8')

if __name__=='__main__':main()
