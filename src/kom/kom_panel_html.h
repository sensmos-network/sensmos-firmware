#pragma once
// Panel komunikatora — jedna strona, bez zasobów z zewnątrz (działa bez internetu).
#include <Arduino.h>
static const char KOM_PANEL_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="pl"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Komunikator Sensmos</title>
<style>
:root{--bg:#f4f5f7;--card:#fff;--fg:#1b1d21;--mut:#687080;--acc:#0a7d5a;--err:#b3261e;--bd:#dde0e5}
@media(prefers-color-scheme:dark){:root{--bg:#111316;--card:#1b1e23;--fg:#eceef1;--mut:#9aa1ad;--acc:#3cc38f;--err:#ff8a80;--bd:#2c3038}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--fg);font:16px/1.45 system-ui,sans-serif;padding:0 16px 88px}
header{padding:18px 0 8px}header b{font-size:18px}header small{display:block;color:var(--mut)}
section{display:none}section.on{display:block}
.card{background:var(--card);border:1px solid var(--bd);border-radius:12px;padding:16px;margin:12px 0}
h2{font-size:17px;margin:0 0 8px}p.h{color:var(--mut);margin:4px 0 12px;font-size:14px}
label{display:block;font-size:14px;color:var(--mut);margin:12px 0 4px}
input{width:100%;padding:12px;font-size:16px;border:1px solid var(--bd);border-radius:8px;background:var(--bg);color:var(--fg)}
button{width:100%;padding:13px;font-size:16px;border:0;border-radius:8px;background:var(--acc);color:#fff;margin-top:14px;font-weight:600}
button.sec{background:transparent;color:var(--acc);border:1px solid var(--acc)}button.bad{background:var(--err)}
button:disabled{opacity:.45}
.big{font:700 28px ui-monospace,monospace;letter-spacing:1px}.mono{font-family:ui-monospace,monospace}
.row{display:flex;justify-content:space-between;padding:6px 0;border-bottom:1px solid var(--bd);font-size:15px}.row:last-child{border:0}
.row span:first-child{color:var(--mut)}
.bar{height:8px;background:var(--bd);border-radius:4px;overflow:hidden;margin-top:6px}.bar i{display:block;height:100%;background:var(--acc)}
.opt{display:flex;gap:12px;align-items:flex-start;padding:12px;border:1px solid var(--bd);border-radius:10px;margin:8px 0}
.opt input{width:auto;margin-top:4px}.opt b{display:block}.opt small{color:var(--mut)}
.net{display:flex;justify-content:space-between;align-items:center;width:100%;padding:14px;margin:6px 0;border:1px solid var(--bd);border-radius:10px;background:var(--bg);color:var(--fg);font-size:16px;text-align:left}
.net.on{border-color:var(--acc);outline:2px solid var(--acc)}.net small{color:var(--mut)}
.tpl{display:flex;gap:8px;align-items:center}.tpl button{width:auto;margin:0;padding:12px 14px}
#msg{position:fixed;left:16px;right:16px;bottom:78px;padding:12px;border-radius:8px;background:var(--fg);color:var(--bg);display:none}
nav{position:fixed;left:0;right:0;bottom:0;display:none;background:var(--card);border-top:1px solid var(--bd)}
nav.on{display:flex}nav a{flex:1;text-align:center;padding:14px 4px;color:var(--mut);text-decoration:none;font-size:14px}nav a.on{color:var(--acc);font-weight:600}
</style></head><body>
<header><b>Komunikator Sensmos</b><small id="hd">…</small></header>

<section id="s-wifi1"><div class="card"><h2>Wybierz swoją sieć WiFi</h2>
<p class="h">Urządzenie dołączy do niej na stałe. Potem w apce Sensmos: Dodaj urządzenie LoRa → Sparuj.</p>
<div id="nets"></div>
<button class="sec" onclick="scan(1)">Odśwież listę</button>
<div id="w1f" style="display:none"><label id="w1l">Hasło</label><input id="w1p" type="password" maxlength="64">
<input id="w1s" maxlength="32" autocapitalize="off" placeholder="Nazwa sieci (SSID)" style="display:none;margin-top:8px">
<button onclick="wifi1()">Połącz</button></div>
<p class="h"><a href="#" onclick="other();return false">Inna sieć (ukryta)</a></p>
<p class="h" id="w1h"></p></div></section>

<section id="s-pin"><div class="card"><h2>Ustaw PIN panelu</h2>
<p class="h">PIN chroni ustawienia urządzenia. 4–8 cyfr.</p>
<label>PIN</label><input id="p1" type="password" inputmode="numeric" maxlength="8">
<label>Powtórz PIN</label><input id="p2" type="password" inputmode="numeric" maxlength="8">
<button onclick="setPin()">Zapisz PIN</button></div></section>

<section id="s-login"><div class="card"><h2>Zaloguj</h2>
<label>PIN</label><input id="lp" type="password" inputmode="numeric" maxlength="8">
<button onclick="login()">Dalej</button></div></section>

<section id="s-vis"><div class="card"><h2>Kto może Cię zobaczyć?</h2>
<p class="h">Wybierz jedną opcję. Możesz to później zmienić w Ustawieniach.</p>
<div id="visbox"></div>
<button id="visok" onclick="saveVis('visbox')" disabled>Zapisz</button></div></section>

<section id="s-start"><div class="card"><label>ID urządzenia</label><div class="big" id="id8"></div>
<label>Odcisk klucza</label><div class="mono" id="fp"></div></div>
<div class="card"><h2>Wiadomości z konta</h2><div id="inb"></div></div>
<div class="card" id="st"></div>
<div class="card"><label>Pasmo 868.1 w ostatniej godzinie</label><div id="dt"></div><div class="bar"><i id="db"></i></div>
<button class="sec" onclick="hello()">Wyślij HELLO teraz</button></div></section>


<section id="s-tpl"><div class="card"><h2>Wyślij do mojego konta</h2>
<p class="h">Trafia do Twojego Home Assistant (zdarzenie <span class="mono">sensmos_device_message</span>) i do apki Sensmos. Działa po sparowaniu urządzenia z portfelem w apce.</p>
<label>Treść</label><input id="bt" maxlength="100" placeholder="np. kod:ALARM">
<button onclick="send(val('bt'))">Wyślij teraz</button></div>
<div class="card"><h2>Szablony</h2>
<p class="h">Gotowe teksty. Pierwszy wysyłasz dwukrotnym naciśnięciem przycisku PRG.</p>
<div id="tl"></div><button onclick="saveTpl()">Zapisz szablony</button></div></section>

<section id="s-set"><div class="card"><h2>Nazwa</h2><label>Nazwa urządzenia (do 16 znaków)</label><input id="nm" maxlength="16">
<button onclick="saveName()">Zapisz nazwę</button></div>
<div class="card"><h2>Widoczność</h2><div id="visbox2"></div><button onclick="saveVis('visbox2')">Zapisz widoczność</button></div>
<div class="card"><h2>Sieć domowa (LAN)</h2><p class="h" id="lanh"></p>
<label>Nazwa sieci (SSID)</label><input id="ws" maxlength="32" autocapitalize="off">
<label>Hasło</label><input id="wp" type="password" maxlength="64">
<button onclick="saveWifi()">Zapisz sieć</button></div>
<div class="card"><h2>Zmień PIN</h2><label>Obecny PIN</label><input id="op" type="password" inputmode="numeric" maxlength="8">
<label>Nowy PIN</label><input id="np" type="password" inputmode="numeric" maxlength="8">
<button onclick="chPin()">Zmień PIN</button></div>
<div class="card"><button class="sec" onclick="off()">Wyłącz panel</button>
<label>Reset fabryczny: nowy klucz i nowe ID, wszystko zostanie skasowane. Wpisz RESET</label><input id="rs">
<button class="bad" onclick="reset()">Resetuj urządzenie</button></div></section>

<div id="msg"></div>
<nav id="nv"><a data-s="start">Start</a><a data-s="tpl">Wiadomości</a><a data-s="set">Ustawienia</a></nav>
<script>
const $=i=>document.getElementById(i),val=i=>$(i).value.trim();
let S=sessionStorage.getItem('ks')||'',ST={},timer;
const ERR={pin_format:'PIN: 4–8 cyfr',pin:'Zły PIN',old_pin:'Zły obecny PIN',budget:'Brak pasma (limit 1% na godzinę) — spróbuj za kilka minut',
text:'Pusta albo za długa treść (do 100 bajtów)',name:'Nazwa: do 16 bajtów',wifi:'SSID do 32 znaków, hasło 8–64 albo puste',
vis:'Wybierz jedną opcję',radio:'Błąd radia',json:'Błąd danych',confirm:'Wpisz RESET'};
const VIS=[['Ukryty','Nie ma Cię na mapie ani w „w pobliżu”. Sieć i tak przenosi Twoje wiadomości.'],
['W pobliżu','Inne komunikatory słyszane przez te same bramy widzą Cię na liście „w pobliżu”.'],
['Na mapie','Kropka na publicznej mapie przy bramie, która Cię słyszy (przybliżona, bez GPS).']];
const clean=s=>{s=s.replace(/[\u0000-\u001f\u007f-\u009f]/g,'');while(new TextEncoder().encode(s).length>100)s=s.slice(0,-1);return s};
function msg(t){const m=$('msg');m.textContent=t;m.style.display='block';clearTimeout(m.t);m.t=setTimeout(()=>m.style.display='none',3500)}
async function api(p,b){const r=await fetch('/api/'+p,{method:b?'POST':'GET',headers:{'X-Kom-Session':S,'Content-Type':'application/json'},body:b?JSON.stringify(b):undefined});
let j={};try{j=await r.json()}catch(e){}
if(r.status==401&&j.err=='session'){S='';sessionStorage.removeItem('ks');show('login');throw 0}
if(!r.ok){msg(j.err=='locked'?'Za dużo prób — poczekaj '+j.wait+' s':(ERR[j.err]||('Błąd '+r.status)));throw 0}return j}
function show(s){document.querySelectorAll('section').forEach(e=>e.classList.toggle('on',e.id=='s-'+s));
const inApp=['start','tpl','set'].includes(s);$('nv').classList.toggle('on',inApp);
document.querySelectorAll('nav a').forEach(a=>a.classList.toggle('on',a.dataset.s==s));
clearInterval(timer);if(s=='start'){load();timer=setInterval(load,5000)}if(s=='set'||s=='tpl')fill()}
document.querySelectorAll('nav a').forEach(a=>a.onclick=()=>show(a.dataset.s));
function visBox(id,cur){$(id).innerHTML=VIS.map((v,i)=>`<label class="opt"><input type="radio" name="${id}" value="${i}" ${cur===i?'checked':''}><span><b>${v[0]}</b><small>${v[1]}</small></span></label>`).join('');
$(id).onchange=()=>{if($('visok'))$('visok').disabled=false}}
async function start(){const j=await(await fetch('/api/id')).json();$('hd').textContent='ID '+j.id8+(j.name?' · '+j.name:'');
if(!j.wifi_set){show('wifi1');return scan(0)}if(!j.pin_set)return show('pin');if(!S)return show('login');await load();ST.vis==255?(visBox('visbox',-1),show('vis')):show('start')}
const bars=r=>r>-55?'▂▄▆█':r>-67?'▂▄▆':r>-78?'▂▄':'▂';
async function scan(r){const j=await(await fetch('/api/scan'+(r?'?refresh=1':''))).json();$('nets').innerHTML=j.nets.sort((a,b)=>b.rssi-a.rssi).map((n,i)=>`<button class="net" data-i="${i}"><span>${n.lock?'🔒 ':''}${n.ssid.replace(/</g,'&lt;')}</span><small>${bars(n.rssi)}</small></button>`).join('')||'<p class="h">Szukam sieci…</p>';document.querySelectorAll('.net').forEach(b=>b.onclick=()=>pick(j.nets[+b.dataset.i],b));if(j.scanning||r)setTimeout(()=>scan(0),2500)}
function pick(n,b){document.querySelectorAll('.net').forEach(x=>x.classList.remove('on'));b.classList.add('on');$('w1s').value=n.ssid;$('w1s').style.display='none';$('w1l').textContent='Hasło do „'+n.ssid+'”';$('w1f').style.display='block';$('w1p').focus()}
function other(){$('w1s').value='';$('w1s').style.display='block';$('w1l').textContent='Hasło';$('w1f').style.display='block';$('w1s').focus()}
async function wifi1(){if(!val('w1s'))return msg(ERR.wifi);await api('wifi',{ssid:val('w1s'),pass:$('w1p').value});$('w1p').value='';$('w1f').style.display='none';$('w1h').textContent='Zapisane. Urządzenie łączy się z „'+val('w1s')+'”, a telefon za chwilę sam rozłączy się z SENSMOS — to normalne. Wróć do domowego WiFi i dokończ w apce Sensmos: Dodaj urządzenie LoRa → Sparuj.'}
async function setPin(){if(val('p1')!=val('p2'))return msg('PIN-y się różnią');const j=await api('pin',{pin:val('p1')});S=j.session;sessionStorage.setItem('ks',S);start()}
async function login(){const j=await api('login',{pin:val('lp')});S=j.session;sessionStorage.setItem('ks',S);$('lp').value='';start()}
async function load(){ST=await api('status');$('id8').textContent=ST.id8;$('fp').textContent=ST.fp;
const ago=ST.hello_ago_s==null?'jeszcze nie':Math.floor(ST.hello_ago_s/60)+' min temu';
$('st').innerHTML=[['Radio',ST.radio?'OK ('+ST.board+')':'brak'],['Self-test',ST.selftest?'OK':'BŁĄD — nie nadaje'],['HELLO',ST.hello_n+', '+ago],
['Odebrane ramki',ST.rx_n+(ST.rx_n?', ostatnia '+Math.round(ST.rx_rssi)+' dBm':'')],['Widoczność',ST.vis==255?'nie wybrano':VIS[ST.vis][0]],
['Oprogramowanie',ST.fw]].map(r=>`<div class="row"><span>${r[0]}</span><span>${r[1]}</span></div>`).join('');
$('inb').innerHTML=(ST.inbox||[]).map(m=>`<div class="row"><span>${m.text.replace(/</g,'&lt;')}</span><span>${Math.floor(m.ago_s/60)} min</span></div>`).join('')||'<p class="h">Brak — napisz z apki Sensmos.</p>';
$('dt').textContent=(ST.duty_ms/1000).toFixed(1)+' s z '+(ST.duty_max/1000)+' s';$('db').style.width=Math.min(100,100*ST.duty_ms/ST.duty_max)+'%'}
async function fill(){await load();$('nm').value=ST.name||'';visBox('visbox2',ST.vis);$('ws').value=ST.wifi.ssid||'';
$('lanh').textContent=ST.wifi.ssid?(ST.wifi.lan_ip?'Połączono: http://'+ST.wifi.lan_ip+' albo http://'+ST.wifi.host:'Łączę z „'+ST.wifi.ssid+'”…'):'Po zapisaniu panel będzie dostępny też w Twojej sieci domowej.';
let h='';for(let i=0;i<5;i++)h+=`<label>${i==0?'1 — pod przycisk (2× PRG)':(i+1)}</label><div class="tpl"><input id="t${i}" maxlength="100" value="${(ST.tpl[i]||'').replace(/"/g,'')}"><button class="sec" onclick="send(val('t${i}'))">Wyślij</button></div>`;$('tl').innerHTML=h}
async function saveVis(id){const c=document.querySelector(`input[name=${id}]:checked`);if(!c)return msg(ERR.vis);await api('vis',{vis:+c.value});msg('Zapisano');ST.vis=+c.value;if(id=='visbox')show('start')}
async function saveName(){const n=val('nm');if(new TextEncoder().encode(n).length>16)return msg(ERR.name);await api('name',{name:n});msg('Zapisano');start()}
async function saveTpl(){const l=[];for(let i=0;i<5;i++){const t=clean(val('t'+i));if(t)l.push(t)}await api('templates',{list:l});msg('Zapisano');fill()}
async function send(t){t=clean(t||'');if(!t)return msg(ERR.text);const j=await api('send',{text:t});msg('Wysłano ('+j.air_ms+' ms w eterze)');load()}
async function hello(){await api('hello',{});msg('HELLO pójdzie za chwilę')}
async function saveWifi(){await api('wifi',{ssid:val('ws'),pass:$('wp').value});$('wp').value='';msg('Zapisano — łączę z siecią');setTimeout(fill,8000)}
async function chPin(){await api('pin',{old:val('op'),pin:val('np')}).then(j=>{S=j.session;sessionStorage.setItem('ks',S);msg('PIN zmieniony')});$('op').value=$('np').value=''}
async function off(){await api('panel_off',{});msg('Panel wyłączony');}
async function reset(){if(val('rs')!='RESET')return msg(ERR.confirm);await api('reset',{confirm:'RESET'});msg('Reset — urządzenie uruchamia się od nowa')}
start().catch(()=>{});
</script></body></html>)HTML";
