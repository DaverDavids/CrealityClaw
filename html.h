#pragma once
#include <Arduino.h>

// ============================================================
//  html.h  –  All HTML page content for Creality Claw Machine
//  Separated from main sketch for readability.
//  Call buildRootHTML() and buildConfigHTML() from the handlers.
// ============================================================

// ---- ROOT PAGE (/) -----------------------------------------

static const char ROOT_HTML_HEAD[] PROGMEM =
  "<!DOCTYPE html><html><head><meta charset=\"UTF-8\">"
  "<title>\xF0\x9F\x8E\xAE Claw Controller</title><style>"
  ".uptime{font-size:13px;color:#95a5a6;margin-top:5px;}"
  "*{margin:0;padding:0;box-sizing:border-box;}"
  "body{font-family:Arial,sans-serif;background:#2c3e50;color:#ecf0f1;padding:20px;}"
  ".container{max-width:1000px;margin:0 auto;}"
  ".header-bar{background:#34495e;padding:15px;border-radius:8px;margin-bottom:20px;"
    "display:flex;justify-content:space-between;align-items:center;"
    "box-shadow:0 4px 6px rgba(0,0,0,0.3);}"
  ".pos-display{font-family:'Courier New',monospace;font-size:18px;color:#f1c40f;font-weight:bold;}"
  ".nav-link{color:#3498db;text-decoration:none;margin-left:15px;font-size:14px;}"
  ".nav-link:hover{text-decoration:underline;}"
  ".panel{background:#34495e;border-radius:8px;padding:20px;margin-bottom:20px;"
    "box-shadow:0 4px 6px rgba(0,0,0,0.3);}"
  "#console{background:#000;color:#2ecc71;font-family:'Courier New',monospace;padding:10px;"
    "height:250px;overflow-y:auto;margin-bottom:10px;font-size:13px;border:1px solid #7f8c8d;}"
  ".tx-line{color:#3498db;}.rx-line{color:#2ecc71;}.blocked-line{color:#e74c3c;}"
  ".controls{display:flex;gap:10px;margin-bottom:10px;flex-wrap:wrap;}"
  "button{padding:12px 20px;border:none;border-radius:6px;cursor:pointer;"
    "font-size:14px;font-weight:bold;transition:0.2s;}"
  "button:active{transform:scale(0.98);}"
  ".btn-primary{background:#2980b9;color:#fff;}.btn-danger{background:#c0392b;color:#fff;}"
  ".btn-success{background:#27ae60;color:#fff;}.btn-warning{background:#f39c12;color:#fff;}"
  ".btn-info{background:#8e44ad;color:#fff;}"
  "input[type=number],input[type=text]{padding:8px;border-radius:4px;border:none;"
    "background:#ecf0f1;color:#2c3e50;}"
  ".bed-section{text-align:center;}"
  ".bed-section h3{margin-bottom:15px;color:#3498db;}"
  "#bedCanvas{border:2px solid #7f8c8d;background:#1a1a1a;cursor:crosshair;"
    "border-radius:4px;box-shadow:0 4px 6px rgba(0,0,0,0.3);}"
  ".bed-controls{display:flex;gap:10px;justify-content:center;align-items:center;"
    "margin-top:15px;flex-wrap:wrap;}"
  ".coord-input{display:flex;align-items:center;gap:5px;}"
  ".coord-input label{font-weight:bold;color:#f1c40f;}"
  ".coord-input input{width:80px;text-align:center;}"
  ".movement-container{display:flex;gap:20px;justify-content:center;"
    "align-items:center;flex-wrap:wrap;}"
  ".servo-control{background:#2c3e50;padding:15px;border-radius:6px;width:180px;}"
  ".servo-control h4{margin-bottom:10px;text-align:center;color:#27ae60;}"
  ".slider{width:100%;height:8px;border-radius:5px;background:#7f8c8d;"
    "outline:none;margin:10px 0;}"
  ".servo-buttons{display:flex;gap:5px;margin-top:10px;}"
  ".servo-buttons button{padding:8px 12px;font-size:12px;flex:1;}"
  ".angle-display{text-align:center;font-size:18px;color:#f1c40f;margin:5px 0;}"
  ".movement-grid{display:grid;grid-template-columns:repeat(5,1fr);gap:10px;max-width:600px;}"
  ".movement-grid button{height:60px;font-size:16px;}"
  ".pos-1{grid-column:1;}.pos-2{grid-column:2;}.pos-3{grid-column:3;}"
  ".pos-4{grid-column:4;}.pos-5{grid-column:5;}"
  ".macro-section{background:#1a3a1a;padding:15px;border-radius:6px;margin-top:15px;}"
  ".macro-item{display:inline-block;margin:5px;}"
  ".btn-macro{background:#27ae60;color:#fff;margin:5px;padding:10px 15px;}"
  ".btn-macro-edit{background:#f39c12;color:#fff;padding:6px 10px;font-size:11px;margin-left:5px;}"
  "</style></head><body><div class=\"container\">";

static const char ROOT_HTML_HEADER[] PROGMEM =
  "<div class=\"header-bar\"><div><h2>\xF0\x9F\x8E\xAE Claw Controller</h2>"
  "<a href=\"/config\" class=\"nav-link\">\xE2\x9A\x99\xEF\xB8\x8F Twitch Config</a></div>"
  "<div class=\"uptime\" id=\"uptimeDisplay\">Uptime: --</div>"
  "<div class=\"pos-display\" id=\"posDisplay\">X:0.0 Y:0.0 Z:0.0</div></div>";

static const char ROOT_HTML_CONSOLE[] PROGMEM =
  "<div class=\"panel\"><div id=\"console\"></div><div class=\"controls\">"
  "<input type=\"text\" id=\"commandInput\" placeholder=\"M119...\" style=\"flex:1;\">"
  "<button class=\"btn-primary\" onclick=\"sendCommand()\">\xF0\x9F\x93\xA4 Send</button>"
  "<button class=\"btn-warning\" onclick=\"clearConsole()\">Clear Log</button></div></div>";

static const char ROOT_HTML_CONTROLS[] PROGMEM =
  "<div class=\"panel\"><div style=\"text-align:center;margin-bottom:20px;\">"
  "<button class=\"btn-danger\" style=\"width:100%;font-size:18px;\" onclick=\"emergencyStop()\">"
    "\xF0\x9F\x9B\x91 EMERGENCY STOP</button></div>"
  "<div class=\"controls\" style=\"justify-content:center;\">"
  "<label>Dist: <input type=\"number\" id=\"distance\" value=\"40\" style=\"width:70px;\"></label>"
  "<label>Speed: <input type=\"number\" id=\"speed\" value=\"7000\" style=\"width:80px;\"></label>"
  "<label>Z-Dist: <input type=\"number\" id=\"zDistance\" value=\"40\" style=\"width:70px;\"></label></div>"
  "<div class=\"movement-container\">"
  "<div class=\"servo-control\">"
  "<h4>\xF0\x9F\xA6\xBE CLAW</h4>"
  "<div class=\"angle-display\"><span id=\"angleDisplay\">90</span>\xC2\xB0</div>"
  "<input type=\"range\" min=\"0\" max=\"180\" value=\"90\" class=\"slider\" id=\"servoSlider\""
    " oninput=\"updateAngleDisplay(this.value)\">"
  "<div class=\"servo-buttons\">"
  "<button class=\"btn-success\" onclick=\"moveClaw('open')\">OPEN</button>"
  "<button class=\"btn-success\" onclick=\"moveClaw('close')\">CLOSE</button>"
  "</div>"
  "<button class=\"btn-info\" style=\"width:100%;margin-top:5px;\" onclick=\"moveClawAngle()\">Set Angle</button>"
  "</div>"
  "<div class=\"movement-grid\">"
  "<button class=\"btn-warning pos-1\" onclick=\"sendCommand('M42 P0 S255 I1')\">LIGHT ON</button>"
  "<button class=\"pos-2\"></button>"
  "<button class=\"btn-primary pos-3\" onclick=\"moveAxis('Y',1)\">Y+</button>"
  "<button class=\"pos-4\"></button>"
  "<button class=\"btn-info pos-5\" onclick=\"moveAxis('Z',1)\">Z+</button>"
  "<button class=\"btn-warning pos-1\" onclick=\"toggleLight()\">LIGHT TOGGLE</button>"
  "<button class=\"btn-primary pos-2\" onclick=\"moveAxis('X',-1)\">X-</button>"
  "<button class=\"btn-warning pos-3\" onclick=\"disableMotors()\">OFF</button>"
  "<button class=\"btn-primary pos-4\" onclick=\"moveAxis('X',1)\">X+</button>"
  "<button class=\"btn-info pos-5\" onclick=\"sendCommand('G28 Z')\">Z HOME</button>"
  "<button class=\"btn-warning pos-1\" onclick=\"sendCommand('M42 P0 S0 I1')\">LIGHT OFF</button>"
  "<button class=\"pos-2\"></button>"
  "<button class=\"btn-primary pos-3\" onclick=\"moveAxis('Y',-1)\">Y-</button>"
  "<button class=\"pos-4\"></button>"
  "<button class=\"btn-info pos-5\" onclick=\"moveAxis('Z',-1)\">Z-</button>"
  "</div></div>"
  "<div class=\"macro-section\"><h3>\xE2\x9A\xA1 Macros</h3>"
  "<div id=\"macroList\"></div>"
  "<div style=\"margin-top:15px;\">"
  "<input type=\"text\" id=\"macroName\" placeholder=\"Macro name\" style=\"width:150px;\">"
  "<input type=\"text\" id=\"macroCommand\" placeholder=\"Command\" style=\"width:250px;\">"
  "<button class=\"btn-success\" onclick=\"saveMacro()\">\xF0\x9F\x92\xBE Save Macro</button>"
  "<button class=\"btn-danger\" onclick=\"deleteMacroByName()\">\xF0\x9F\x97\x91\xEF\xB8\x8F Delete Macro</button>"
  "</div></div>"
  "<div class=\"controls\" style=\"justify-content:center;margin-top:20px;\">"
  "<button class=\"btn-primary\" onclick=\"homeAll()\">Home ALL</button>"
  "<button class=\"btn-primary\" onclick=\"homeXY()\">Home XY</button>"
  "<button class=\"btn-info\" onclick=\"updatePosition()\">Refresh Pos</button></div>"
  "<div class=\"panel bed-section\" style=\"margin-top:20px;\">"
  "<h3>\xF0\x9F\x97\xBA\xEF\xB8\x8F Bed Position Map</h3>"
  "<canvas id=\"bedCanvas\" width=\"400\" height=\"380\"></canvas>"
  "<div class=\"bed-controls\">"
  "<div class=\"coord-input\"><label>X:</label>"
    "<input type=\"number\" id=\"bedX\" value=\"50\" min=\"0\" max=\"100\" step=\"0.1\">"
    "<label>%</label></div>"
  "<div class=\"coord-input\"><label>Y:</label>"
    "<input type=\"number\" id=\"bedY\" value=\"50\" min=\"0\" max=\"100\" step=\"0.1\">"
    "<label>%</label></div>"
  "<button class=\"btn-primary\" onclick=\"moveToPercent()\">\xF0\x9F\x93\x8D Move To Position</button>"
  "</div></div></div></div>";

// JS is split into two parts around the dynamic MIN_X/Y MAX_X/Y constants
static const char ROOT_HTML_JS_PRE[] PROGMEM =
  "<script>"
  "const MIN_X=";   // followed by String(MIN_X,1)

static const char ROOT_HTML_JS_MID1[] PROGMEM = ";const MIN_Y=";  // + String(MIN_Y,1)
static const char ROOT_HTML_JS_MID2[] PROGMEM = ";const MAX_X=";  // + String(MAX_X,1)
static const char ROOT_HTML_JS_MID3[] PROGMEM = ";const MAX_Y=";  // + String(MAX_Y,1)

static const char ROOT_HTML_JS_POST[] PROGMEM =
  ";"
  "var consoleDiv=document.getElementById('console');"
  "var posDiv=document.getElementById('posDisplay');"
  "var lastConsoleHTML='';"
  "var currentX=0,currentY=0;"
  "var canvas=document.getElementById('bedCanvas');"
  "var ctx=canvas.getContext('2d');"
  "function drawBed(){"
    "ctx.fillStyle='#1a1a1a';ctx.fillRect(0,0,400,380);"
    "ctx.strokeStyle='#7f8c8d';ctx.lineWidth=2;"
    "ctx.strokeRect(20,20,360,340);"
    "ctx.strokeStyle='#3498db';ctx.lineWidth=1;ctx.setLineDash([5,5]);"
    "for(var i=1;i<4;i++){var x=20+i*90;ctx.beginPath();ctx.moveTo(x,20);ctx.lineTo(x,360);ctx.stroke();}"
    "for(var i=1;i<4;i++){var y=20+i*85;ctx.beginPath();ctx.moveTo(20,y);ctx.lineTo(380,y);ctx.stroke();}"
    "ctx.setLineDash([]);"
    "ctx.fillStyle='#ecf0f1';ctx.font='12px Arial';"
    "ctx.fillText('0,0',5,375);"
    "ctx.fillText('100,0',355,375);"
    "ctx.fillText('0,100',5,30);"
    "ctx.fillText('100,100',345,30);"
    "var px=20+(currentX-MIN_X)/(MAX_X-MIN_X)*360;"
    "var py=360-(currentY-MIN_Y)/(MAX_Y-MIN_Y)*340;"
    "ctx.fillStyle='#e74c3c';ctx.beginPath();ctx.arc(px,py,8,0,2*Math.PI);ctx.fill();"
    "ctx.strokeStyle='#fff';ctx.lineWidth=2;ctx.stroke();"
  "}"
  "canvas.addEventListener('click',function(e){"
    "var rect=canvas.getBoundingClientRect();"
    "var x=e.clientX-rect.left;"
    "var y=e.clientY-rect.top;"
    "if(x<20||x>380||y<20||y>360)return;"
    "var percentX=((x-20)/360*100).toFixed(1);"
    "var percentY=(100-(y-20)/340*100).toFixed(1);"
    "document.getElementById('bedX').value=percentX;"
    "document.getElementById('bedY').value=percentY;"
    "moveToPercent();"
  "});"
  "function moveToPercent(){"
    "var xPercent=parseFloat(document.getElementById('bedX').value);"
    "var yPercent=parseFloat(document.getElementById('bedY').value);"
    "xPercent=Math.max(0,Math.min(100,xPercent));"
    "yPercent=Math.max(0,Math.min(100,yPercent));"
    "var targetX=MIN_X+(MAX_X-MIN_X)*xPercent/100;"
    "var targetY=MIN_Y+(MAX_Y-MIN_Y)*yPercent/100;"
    "var cmd='G1 X'+targetX.toFixed(2)+' Y'+targetY.toFixed(2)+' F3000';"
    "sendCommand(cmd);"
    "currentX=targetX;currentY=targetY;drawBed();"
  "}"
  "document.getElementById('commandInput').addEventListener('keypress',function(e){"
    "if(e.key==='Enter')sendCommand();"
  "});"
  "function updateState(){"
    "fetch('/state').then(r=>r.json()).then(d=>{"
      "if(d.uptime){document.getElementById('uptimeDisplay').textContent='Uptime: '+d.uptime;}"
      "if(d.pos){posDiv.textContent='X:'+d.pos.X.toFixed(1)+' Y:'+d.pos.Y.toFixed(1)+' Z:'+d.pos.Z.toFixed(1);}"
      "if(d.messages){var o='';for(var i=0;i<d.messages.length;i++){var m=d.messages[i];"
        "var cls=m.type=='tx'?'tx-line':(m.type=='blocked'?'blocked-line':'rx-line');"
        "o+='<div class=\"'+cls+'\">'+m.time+' '+m.text.replace(/</g,'&lt;')+'</div>';}"
        "if(lastConsoleHTML!=o){consoleDiv.innerHTML=o;consoleDiv.scrollTop=consoleDiv.scrollHeight;lastConsoleHTML=o;}}"
    "}).catch(e=>console.error(e));}"
  "function updateAngleDisplay(v){document.getElementById('angleDisplay').textContent=v;}"
  "function sendCommand(c){"
    "if(!c)c=document.getElementById('commandInput').value;"
    "if(c)fetch('/send?cmd='+encodeURIComponent(c));"
    "if(!c.includes('M42'))document.getElementById('commandInput').value='';"
    "setTimeout(updateState,50);}"
  "function moveAxis(a,d){"
    "var dist=(a=='Z')?document.getElementById('zDistance').value:document.getElementById('distance').value;"
    "var speed=document.getElementById('speed').value;"
    "var actualDist=d*parseFloat(dist);"
    "fetch('/move?axis='+a+'&distance='+actualDist+'&speed='+parseInt(speed));setTimeout(updateState,50);}"
  "function moveClaw(a){fetch('/claw?action='+a);}"
  "function moveClawAngle(){var angle=document.getElementById('servoSlider').value;fetch('/claw?angle='+angle);}"
  "function clearConsole(){fetch('/clear').then(updateState);}"
  "function emergencyStop(){fetch('/send?cmd=M112');setTimeout(function(){fetch('/send?cmd=M999');},100);}"
  "function homeAll(){fetch('/send?cmd=G28');}"
  "function homeXY(){fetch('/send?cmd=G28 X Y');}"
  "function disableMotors(){fetch('/send?cmd=M84');}"
  "function updatePosition(){"
    "fetch('/updatepos').then(r=>r.json()).then(d=>{"
      "if(d.pos){currentX=d.pos.X;currentY=d.pos.Y;drawBed();}"
    "});}"
  "function toggleLight(){fetch('/togglelight');}"
  "function saveMacro(){"
    "var n=document.getElementById('macroName').value;"
    "var c=document.getElementById('macroCommand').value;"
    "if(!n||!c)return;"
    "fetch('/save-macro?name='+encodeURIComponent(n)+'&cmd='+encodeURIComponent(c)).then(function(){"
      "document.getElementById('macroName').value='';"
      "document.getElementById('macroCommand').value='';"
      "loadMacros();});}"
  "function runMacro(c){sendCommand(c);setTimeout(updateState,50);}"
  "function editMacro(n,c){"
    "document.getElementById('macroName').value=n;"
    "document.getElementById('macroCommand').value=c;}"
  "function deleteMacroByName(){"
    "var n=document.getElementById('macroName').value;"
    "if(!n){alert('Enter macro name to delete');return;}"
    "if(confirm('Delete macro '+n+'?')){"
      "fetch('/delete-macro?name='+encodeURIComponent(n)).then(function(){"
        "document.getElementById('macroName').value='';"
        "document.getElementById('macroCommand').value='';"
        "loadMacros();});}}"
  "function loadMacros(){"
    "fetch('/get-macros').then(r=>r.json()).then(function(m){"
      "var list=document.getElementById('macroList');"
      "list.innerHTML='';"
      "for(var key in m){if(m.hasOwnProperty(key)){"
        "var name=key;var cmd=m[key];"
        "var item=document.createElement('div');item.className='macro-item';"
        "var btn=document.createElement('button');btn.className='btn-macro';"
        "btn.textContent=name;"
        "btn.onclick=(function(c){return function(){runMacro(c);};})(cmd);"
        "var editBtn=document.createElement('button');editBtn.className='btn-macro-edit';"
        "editBtn.textContent='\xE2\x9C\x8F\xEF\xB8\x8F';"
        "editBtn.onclick=(function(n,c){return function(){editMacro(n,c);};})(name,cmd);"
        "item.appendChild(btn);item.appendChild(editBtn);"
        "list.appendChild(item);"
      "}}});}"
  "drawBed();"
  "setInterval(updateState,500);"
  "updateState();"
  "loadMacros();"
  "</script></body></html>";


// ---- CONFIG PAGE (/config) ---------------------------------

static const char CONFIG_HTML_HEAD[] PROGMEM =
  "<!DOCTYPE html><html><head><meta charset=\"UTF-8\">"
  "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
  "<title>Twitch Config</title>"
  "<style>"
  "body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,Helvetica,Arial,sans-serif;"
    "background:#1a1a1a;color:#e0e0e0;margin:0;padding:20px;}"
  ".container{max-width:900px;margin:0 auto;}"
  ".header{display:flex;justify-content:space-between;align-items:center;"
    "margin-bottom:30px;border-bottom:1px solid #333;padding-bottom:20px;}"
  "h2{margin:0;color:#a970ff;}" /* Twitch Purple */
  "a{color:#fff;text-decoration:none;background:#333;padding:8px 16px;border-radius:4px;}"
  ".panel{background:#252525;padding:20px;border-radius:8px;margin-bottom:20px;"
    "box-shadow:0 2px 4px rgba(0,0,0,0.3);}"
  "h3{margin-top:0;color:#fff;border-bottom:1px solid #444;padding-bottom:10px;}"
  ".form-group{margin-bottom:15px;}"
  "label{display:block;margin-bottom:5px;font-weight:500;color:#aaa;}"
  "input[type=text],input[type=password],textarea{width:100%;padding:10px;"
    "background:#333;border:1px solid #444;color:#fff;border-radius:4px;box-sizing:border-box;}"
  "input:focus,textarea:focus{border-color:#a970ff;outline:none;}"
  "button{cursor:pointer;padding:10px 20px;border:none;border-radius:4px;"
    "font-weight:bold;color:#fff;transition:opacity 0.2s;}"
  "button:hover{opacity:0.9;}"
  ".btn-primary{background:#a970ff;}"
  ".btn-danger{background:#d32f2f;}"
  ".btn-success{background:#388e3c;}"
  ".row{display:flex;gap:10px;}"
  ".col{flex:1;}"
  ".args-container{display:flex;gap:5px;margin-bottom:10px;}"
  ".args-container input{flex:1;min-width:0;font-size:0.9em;text-align:center;}"
  ".cmd-item{border:1px solid #444;padding:15px;margin-bottom:15px;"
    "border-radius:6px;background:#2a2a2a;}"
  ".cmd-header{display:flex;justify-content:space-between;margin-bottom:10px;}"
  "</style></head><body><div class=\"container\">"
  "<div class=\"header\">"
  "<h2>\xF0\x9F\x91\xBE Twitch Configuration</h2>"
  "<a href=\"/\">\xE2\x86\x90 Back to Dashboard</a>"
  "</div>";

static const char CONFIG_HTML_CONN_PRE[] PROGMEM =
  "<div class=\"panel\"><h3>Connection Settings</h3>"
  "<div class=\"form-group\"><label>Channel Name</label>"
  "<input type=\"text\" id=\"channel\" value=\"";
// dynamic: twitchChannel

static const char CONFIG_HTML_CONN_MID1[] PROGMEM =
  "\"></div>"
  "<div class=\"form-group\"><label>Bot Nickname</label>"
  "<input type=\"text\" id=\"nick\" value=\"";
// dynamic: twitchNick

static const char CONFIG_HTML_CONN_MID2[] PROGMEM =
  "\"></div>"
  "<div class=\"form-group\"><label>OAuth Token (oauth:...)</label>"
  "<input type=\"password\" id=\"oauth\" value=\"";
// dynamic: twitchOauth

static const char CONFIG_HTML_CONN_MID3[] PROGMEM =
  "\"></div>"
  "<div class=\"form-group\">"
  "<label><input type=\"checkbox\" id=\"enabled\" ";
// dynamic: twitchEnabled ? "checked" : ""

static const char CONFIG_HTML_CONN_POST[] PROGMEM =
  "> Enable Twitch Integration</label></div>"
  "<button class=\"btn-primary\" onclick=\"saveConn()\">Save Connection</button>"
  "</div>";

static const char CONFIG_HTML_CMDS[] PROGMEM =
  "<div class=\"panel\"><h3>Chat Commands</h3>"
  "<div id=\"cmdList\"><p>Loading...</p></div>"
  "<hr style=\"border-color:#333;margin:20px 0;\">"
  "<h3>Add New Command</h3>"
  "<div class=\"form-group\"><label>Trigger Word (e.g. !claw)</label>"
  "<input type=\"text\" id=\"newTrigger\" placeholder=\"!claw\"></div>"
  "<div class=\"form-group\"><label>Argument Patterns (up to 5 — e.g. x{X}, y{Y})</label>"
  "<div class=\"args-container\">"
  "<input type=\"text\" id=\"newArg0\" placeholder=\"x{X}\">"
  "<input type=\"text\" id=\"newArg1\" placeholder=\"y{Y}\">"
  "<input type=\"text\" id=\"newArg2\" placeholder=\"\">"
  "<input type=\"text\" id=\"newArg3\" placeholder=\"\">"
  "<input type=\"text\" id=\"newArg4\" placeholder=\"\">"
  "</div></div>"
  "<div class=\"form-group\"><label>Actions (semicolon separated)</label>"
  "<textarea id=\"newActions\" rows=\"3\" "
    "placeholder=\"MOVEPCT:{X},{Y};DELAY:500;CLAW:close;DELAY:1000;CLAW:open;CHAT:@{USER} done!\">"
  "</textarea></div>"
  "<button class=\"btn-success\" onclick=\"addCmd()\">Add Command</button>"
  "</div>";

static const char CONFIG_HTML_JS[] PROGMEM =
  "<script>"
  "function saveConn(){"
    "var d={"
      "channel:document.getElementById('channel').value,"
      "nick:document.getElementById('nick').value,"
      "oauth:document.getElementById('oauth').value,"
      "enabled:document.getElementById('enabled').checked"
    "};"
    "fetch('/twitch-save-conn?channel='+encodeURIComponent(d.channel)"
      "+'&nick='+encodeURIComponent(d.nick)"
      "+'&oauth='+encodeURIComponent(d.oauth)"
      "+'&enabled='+(d.enabled?'1':'0'))"
    ".then(r=>r.text()).then(t=>alert(t));}"
  "function addCmd(){"
    "var trigger=document.getElementById('newTrigger').value;"
    "var actions=document.getElementById('newActions').value;"
    "if(!trigger){alert('Trigger required');return;}"
    "var url='/twitch-add-cmd?trigger='+encodeURIComponent(trigger)"
      "+'&actions='+encodeURIComponent(actions);"
    "for(var i=0;i<5;i++){"
      "var v=document.getElementById('newArg'+i).value;"
      "url+='&arg'+i+'='+encodeURIComponent(v);}"
    "fetch(url).then(r=>r.text()).then(function(t){alert(t);loadCmds();});}"
  "function toggleCmd(i,e){"
    "fetch('/twitch-toggle-cmd?index='+i+'&enabled='+(e?'1':'0'))"
    ".then(r=>r.text()).then(function(t){loadCmds();});}"
  "function delCmd(i){"
    "if(confirm('Delete command?')){"
      "fetch('/twitch-delete-cmd?index='+i)"
      ".then(r=>r.text()).then(function(t){alert(t);loadCmds();})}}"
  "function loadCmds(){"
    "fetch('/twitch-get-cmds').then(r=>r.json()).then(function(cmds){"
      "var h='';"
      "cmds.forEach(function(cmd,i){"
        "h+='<div class=\"cmd-item\">';"
        "h+='<div class=\"cmd-header\">';"
        "h+='<strong>'+cmd.trigger+'</strong>';"
        "h+='<label><input type=\"checkbox\"'+(cmd.enabled?' checked':'')"
          "+' onchange=\"toggleCmd('+i+',this.checked)\"> Enabled</label>';"
        "h+='</div>';"
        "h+='<div style=\"margin-bottom:8px;font-size:0.85em;color:#aaa\">Args: "
          "'+cmd.args.filter(function(a){return a.length>0;}).join(', ')+'</div>';"
        "h+='<div style=\"font-family:monospace;font-size:0.9em;background:#333;"
          "padding:8px;border-radius:4px;margin-bottom:10px;\">'+cmd.actions+'</div>';"
        "h+='<button class=\"btn-danger\" style=\"padding:5px 15px\" onclick=\"delCmd('+i+')\">Delete</button>';"
        "h+='</div></div>';"
      "});"
      "document.getElementById('cmdList').innerHTML=h||'<p>No commands configured.</p>';"
    "});}"
  "loadCmds();"
  "</script></body></html>";


// ============================================================
//  Build functions — called from handleRoot() / handleConfig()
// ============================================================

inline String buildRootHTML(float minX, float minY, float maxX, float maxY) {
  String html;
  html.reserve(14000);
  html += FPSTR(ROOT_HTML_HEAD);
  html += FPSTR(ROOT_HTML_HEADER);
  html += FPSTR(ROOT_HTML_CONSOLE);
  html += FPSTR(ROOT_HTML_CONTROLS);
  html += FPSTR(ROOT_HTML_JS_PRE);
  html += String(minX, 1);
  html += FPSTR(ROOT_HTML_JS_MID1);
  html += String(minY, 1);
  html += FPSTR(ROOT_HTML_JS_MID2);
  html += String(maxX, 1);
  html += FPSTR(ROOT_HTML_JS_MID3);
  html += String(maxY, 1);
  html += FPSTR(ROOT_HTML_JS_POST);
  return html;
}

inline String buildConfigHTML(const String& channel, const String& nick,
                               const String& oauth, bool enabled) {
  String html;
  html.reserve(8000);
  html += FPSTR(CONFIG_HTML_HEAD);
  html += FPSTR(CONFIG_HTML_CONN_PRE);
  html += channel;
  html += FPSTR(CONFIG_HTML_CONN_MID1);
  html += nick;
  html += FPSTR(CONFIG_HTML_CONN_MID2);
  html += oauth;
  html += FPSTR(CONFIG_HTML_CONN_MID3);
  html += enabled ? "checked" : "";
  html += FPSTR(CONFIG_HTML_CONN_POST);
  html += FPSTR(CONFIG_HTML_CMDS);
  html += FPSTR(CONFIG_HTML_JS);
  return html;
}