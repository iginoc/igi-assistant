// src/pkjs/index.js

// Definizioni chiavi AppMessage
var KEY_SENSOR_VALUE = 0;
var KEY_LIGHT_INDEX = 1;
var KEY_LIGHT_NAME = 2;
var KEY_LIGHT_STATE = 3;
var KEY_LIGHT_ENTITY = 4;
var KEY_TOGGLE_LIGHT = 5;
var KEY_SENSOR_UNIT = 6;
var KEY_SENSOR_NAME = 7;
var KEY_CHAT_TEXT = 8;
var KEY_CHAT_ACTIVE = 9;
var KEY_SEND_CHAT = 10;
var KEY_NICKNAME = 11;
var KEY_LAUNCH_APP = 12;
var KEY_MUSIC_TITLE = 13;
var KEY_CONFIG_OPEN = 14;

// Configurazione Home Assistant (Assicurati che corrispondano a quelli nel file HTML)
var haUrl = localStorage.getItem('pebble_ha_url') || ""; 
var haToken = localStorage.getItem('pebble_ha_token') || "";

var savedLights = [];
var chatInterval = null;
var lastChatText = null;

function sendLocalNickname() {
  var nickname = localStorage.getItem('pebble_nickname');
  if (nickname) {
    var dict = {};
    dict[KEY_NICKNAME] = nickname;
    Pebble.sendAppMessage(dict, 
      function() { console.log('Sent local nickname to Pebble.'); },
      function(e) { console.log('Error sending local nickname to Pebble: ' + JSON.stringify(e)); }
    );
  }
}

function sendConfigToPebble() {
  // Prova a recuperare l'utente reale da Home Assistant usando il token
  if (haUrl && haToken) {
    console.log("Fetching user from: " + haUrl + "/api/auth/current_user");
    fetch(haUrl + "/api/auth/current_user", {
      headers: { "Authorization": "Bearer " + haToken, "Content-Type": "application/json" }
    })
    .then(function(response) {
      if (response.ok) return response.json();
      throw new Error('Network response was not ok.');
    })
    .then(function(data) {
      var name = data.name || data.username;
      if (name) {
        console.log("Fetched user from HA: " + name);
        var dict = {};
        dict[KEY_NICKNAME] = name;
        Pebble.sendAppMessage(dict, 
          function() { console.log('Sent HA username to Pebble.'); },
          function(e) { console.log('Error sending HA username to Pebble: ' + JSON.stringify(e)); }
        );
        // Aggiorna il localStorage così appare anche nella pagina di configurazione
        localStorage.setItem('pebble_nickname', name);
      } else {
        sendLocalNickname();
      }
    })
    .catch(function(err) {
      console.log("Error fetching current user: " + err);
      sendLocalNickname();
    });
  } else {
    sendLocalNickname();
  }
}

function sendNextLight(lights, index) {
  if (index >= lights.length) return;

  var light = lights[index];
  var dict = {};
  dict[KEY_LIGHT_INDEX] = index;
  dict[KEY_LIGHT_NAME] = light.attributes.friendly_name || light.entity_id;
  dict[KEY_LIGHT_STATE] = light.state;
  dict[KEY_LIGHT_ENTITY] = light.entity_id;

  Pebble.sendAppMessage(dict, function() {
    console.log('Sent light ' + index);
    sendNextLight(lights, index + 1);
  }, function(e) {
    console.log('Error sending light ' + index + ', retrying...');
    setTimeout(function() { sendNextLight(lights, index); }, 1000);
  });
}

function updateLights() {
  var lightsConfig = JSON.parse(localStorage.getItem('pebble_lights_config') || '[]');
  if (lightsConfig.length === 0) return;

  fetch(haUrl + "/api/states", {
    headers: { "Authorization": "Bearer " + haToken, "Content-Type": "application/json" }
  })
  .then(response => response.json())
  .then(data => {
    // Filtra solo le luci salvate nella configurazione
    var filtered = data.filter(function(e) {
      return lightsConfig.indexOf(e.entity_id) !== -1;
    });
    savedLights = filtered;
    sendNextLight(filtered, 0);
  })
  .catch(err => console.log("Error fetching lights: " + err));
}

function updateSensor() {
  var sensorEntity = localStorage.getItem('pebble_sensor_config') || "sensor.auto_batteria_stimata";
  fetch(haUrl + "/api/states/" + sensorEntity, {
    headers: { "Authorization": "Bearer " + haToken, "Content-Type": "application/json" }
  })
  .then(function(response) {
    if (!response.ok) {
      throw new Error('Network response was not ok ' + response.statusText);
    }
    return response.json();
  })
  .then(function(data) {
    var dict = {};
    var stateWithValue = data.state;
    var unit = data.attributes.unit_of_measurement || "";
    var friendlyName = data.attributes.friendly_name || "";
    if (unit) {
      stateWithValue += " " + unit;
    }
    dict[KEY_SENSOR_VALUE] = stateWithValue;
    dict[KEY_SENSOR_UNIT] = unit;
    dict[KEY_SENSOR_NAME] = friendlyName;
    
    Pebble.sendAppMessage(dict, function() {
      console.log('Sent sensor data: ' + stateWithValue);
    }, function(e) {
      console.log('Error sending sensor data: ' + JSON.stringify(e));
    });
  })
  .catch(function(err) {
    console.log("Error fetching sensor: " + err.message);
  });
}

function toggleLight(entityId) {
  fetch(haUrl + "/api/services/light/toggle", {
    method: "POST",
    headers: { "Authorization": "Bearer " + haToken, "Content-Type": "application/json" },
    body: JSON.stringify({ "entity_id": entityId })
  })
  .then(() => {
    // Dopo il toggle, aggiorna lo stato
    setTimeout(updateLights, 1000);
  })
  .catch(err => console.log("Error toggling light: " + err));
}

function updateChat() {
  fetch(haUrl + "/api/states/input_text.pebble_chat", {
    headers: { "Authorization": "Bearer " + haToken, "Content-Type": "application/json" }
  })
  .then(response => response.json())
  .then(data => {
    var currentText = data.state;
    if (currentText !== lastChatText) {
      lastChatText = currentText;
      var dict = {};
      dict[KEY_CHAT_TEXT] = currentText;
      
      Pebble.sendAppMessage(dict, function() {
        console.log('Sent chat text: ' + currentText);
      }, function(e) {
        console.log('Error sending chat text: ' + JSON.stringify(e));
      });
    }
  })
  .catch(err => console.log("Error fetching chat: " + err));
}

function sendChat(text) {
  fetch(haUrl + "/api/services/input_text/set_value", {
    method: "POST",
    headers: { "Authorization": "Bearer " + haToken, "Content-Type": "application/json" },
    body: JSON.stringify({ "entity_id": "input_text.pebble_chat", "value": text })
  })
  .then(() => console.log("Sent chat to HA: " + text))
  .catch(err => console.log("Error sending chat: " + err));
}

function getActiveMediaPlayer(callback) {
  fetch(haUrl + "/api/states", {
    headers: { "Authorization": "Bearer " + haToken, "Content-Type": "application/json" }
  })
  .then(response => response.json())
  .then(data => {
    var found = null;
    // 1. Cerca player in riproduzione
    for (var i = 0; i < data.length; i++) {
      if (data[i].entity_id.startsWith("media_player.") && data[i].state === 'playing') {
        found = data[i].entity_id;
        break;
      }
    }
    // 2. Se non trovato, cerca il primo disponibile (non unavailable/off)
    if (!found) {
      for (var i = 0; i < data.length; i++) {
        if (data[i].entity_id.startsWith("media_player.") && 
            ['unavailable', 'off', 'idle'].indexOf(data[i].state) === -1) {
          found = data[i].entity_id;
          break;
        }
      }
    }
    // 3. Fallback sull'ultimo trovato
    if (!found) {
       for (var i = 0; i < data.length; i++) {
        if (data[i].entity_id.startsWith("media_player.")) {
          found = data[i].entity_id;
          break;
        }
      }
    }
    
    if (found) callback(found);
    else console.log("No media player found.");
  })
  .catch(err => console.log("Error finding media player: " + err));
}

function updateMusicInfo() {
  getActiveMediaPlayer(function(entityId) {
    fetch(haUrl + "/api/states/" + entityId, {
      headers: { "Authorization": "Bearer " + haToken, "Content-Type": "application/json" }
    })
    .then(response => response.json())
    .then(data => {
      var title = data.attributes.media_title || data.attributes.friendly_name || "No Title";
      var artist = data.attributes.media_artist || "";
      var text = title;
      if (artist) text += "\n" + artist;
      
      var dict = {};
      dict[KEY_MUSIC_TITLE] = text;
      
      Pebble.sendAppMessage(dict, function() {
        console.log('Sent music title: ' + text);
      }, function(e) {
        console.log('Error sending music title: ' + JSON.stringify(e));
      });
    })
    .catch(err => console.log("Error fetching music info: " + err));
  });
}

function controlMusic(command) {
  var service = "";
  if (command === 'music_play_pause') service = "media_play_pause";
  else if (command === 'music_next') service = "media_next_track";
  else if (command === 'music_prev') service = "media_previous_track";
  else if (command === 'volume_up') service = "volume_up";
  else if (command === 'volume_down') service = "volume_down";
  
  if (service) {
    getActiveMediaPlayer(function(entityId) {
      fetch(haUrl + "/api/services/media_player/" + service, {
        method: "POST",
        headers: { "Authorization": "Bearer " + haToken, "Content-Type": "application/json" },
        body: JSON.stringify({ "entity_id": entityId })
      })
      .then(() => {
        console.log("Sent music command " + service + " to " + entityId);
        setTimeout(updateMusicInfo, 1000);
      })
      .catch(err => console.log("Error sending music command: " + err));
    });
  }
}

Pebble.addEventListener('ready', function(e) {
  console.log('PebbleKit JS ready!');
  updateLights();
  updateSensor();
  sendConfigToPebble();
      
  // Aggiorna lo stato ogni 30 secondi
  setInterval(updateLights, 30000);
  setInterval(updateSensor, 5000);
});

Pebble.addEventListener('appmessage', function(e) {
  var dict = e.payload;
  if (dict[KEY_TOGGLE_LIGHT]) {
    console.log("Toggling light: " + dict[KEY_TOGGLE_LIGHT]);
    toggleLight(dict[KEY_TOGGLE_LIGHT]);
  }

  if (dict[KEY_CHAT_ACTIVE] !== undefined) {
    if (dict[KEY_CHAT_ACTIVE]) {
      console.log("Chat mode activated");
      lastChatText = null; // Reset per inviare subito il contenuto attuale
      updateChat();
      if (chatInterval) clearInterval(chatInterval);
      chatInterval = setInterval(updateChat, 2000);
    } else {
      console.log("Chat mode deactivated");
      if (chatInterval) clearInterval(chatInterval);
      chatInterval = null;
    }
  }

  if (dict[KEY_SEND_CHAT]) {
    lastChatText = dict[KEY_SEND_CHAT]; // Aggiorna subito per evitare l'echo dal polling
    sendChat(dict[KEY_SEND_CHAT]);
  }

  if (dict[KEY_LAUNCH_APP]) {
    var cmd = dict[KEY_LAUNCH_APP];
    if (cmd === 'open_music') {
      console.log("Opening built-in Pebble Music App");
      Pebble.openURL('pebble://app/130dd025-420a-5c9f-b02e-692d47923e90');
    } else if (cmd === 'get_music_info') {
      updateMusicInfo();
    } else {
      controlMusic(cmd);
    }
  }
});

function getConfigPage(options) {
  var html = [
    '<!DOCTYPE html>',
    '<html>',
    '<head>',
    '<title>Configurazione Data</title>',
    '<meta name="viewport" content="width=device-width, initial-scale=1">',
    '<style>',
    'body { font-family: sans-serif; padding: 20px; text-align: center; }',
    'input[type="checkbox"], input[type="radio"] { width: auto; margin-right: 10px; transform: scale(1.2); }',
    '.light-item { text-align: left; padding: 10px; border-bottom: 1px solid #eee; }',
    'button { width: 100%; padding: 15px; background: #FF4700; color: white; border: none; border-radius: 8px; font-size: 18px; font-weight: bold; cursor: pointer; }',
    'button:hover { background: #E04000; }',
    '.scroll-box { background: #fafafa; border-radius: 8px; padding: 10px; max-height: 200px; overflow-y: auto; }',
    '</style>',
    '</head>',
    '<body>',
    '<form onsubmit="return false;">',
    '<button id="refresh" type="button" style="margin-bottom: 20px; background-color: #666;">Aggiorna</button>',
    '<div style="margin: 20px 0;"><h3>URL Home Assistant</h3><input type="text" id="ha-url" placeholder="Es: http://192.168.1.10:8123" style="width: 100%; padding: 10px; box-sizing: border-box; border: 1px solid #ccc; border-radius: 5px;" autocapitalize="off" autocorrect="off"></div>',
    '<div style="margin: 20px 0;"><h3>Token (Long-Lived)</h3><input type="text" id="ha-token" placeholder="Es: eyJhbGci..." style="width: 100%; padding: 10px; box-sizing: border-box; border: 1px solid #ccc; border-radius: 5px;" autocapitalize="off" autocorrect="off"></div>',
    '<div style="margin: 20px 0;"><h3>Il tuo Nickname</h3><input type="text" id="nickname" placeholder="Es: Igino" style="width: 100%; padding: 10px; box-sizing: border-box; border: 1px solid #ccc; border-radius: 5px;"></div>',
    '<div style="margin: 20px 0;"><h3>Seleziona Luci</h3><div id="lights-list" class="scroll-box">Caricamento luci...</div></div>',
    '<div style="margin: 20px 0;"><h3>Seleziona Sensore (Grafico)</h3><input type="text" id="sensor-search" placeholder="Cerca sensore..." style="width: 100%; padding: 10px; margin-bottom: 10px; box-sizing: border-box; border: 1px solid #ccc; border-radius: 5px;"><div id="sensors-list" class="scroll-box">Caricamento sensori...</div></div>',
    '<button id="save" type="button">Salva</button>',
    '</form>',
    '<script>',
    'var savedConfig = ' + JSON.stringify(options) + ';',
    'if (savedConfig.nickname) { document.getElementById("nickname").value = savedConfig.nickname; }',
    'if (savedConfig.haUrl) { document.getElementById("ha-url").value = savedConfig.haUrl; }',
    'if (savedConfig.haToken) { document.getElementById("ha-token").value = savedConfig.haToken; }',
    'function fetchResources() {',
    '  var urlInput = document.getElementById("ha-url").value.trim();',
    '  var currentToken = document.getElementById("ha-token").value.trim();',
    '  if (!urlInput || !currentToken) { document.getElementById("lights-list").textContent = "Inserisci URL e Token per caricare le luci."; return; }',
    '  if (!urlInput.startsWith("http")) { urlInput = "http://" + urlInput; document.getElementById("ha-url").value = urlInput; }',
    '  var currentUrl = urlInput.replace(/\\/+$/, "");',
    '  document.getElementById("lights-list").textContent = "Connessione a " + currentUrl + "...";',
    '  fetch(currentUrl + "/api/states", { method: "GET", headers: { "Authorization": "Bearer " + currentToken, "Content-Type": "application/json" } })',
    '  .then(function(response) {',
    '    if (!response.ok) throw new Error("HTTP " + response.status + " " + response.statusText);',
    '    return response.json();',
    '  })',
    '  .then(function(data) {',
    '    var container = document.getElementById("lights-list"); container.innerHTML = "";',
    '    var lights = data.filter(function(e) { return e.entity_id.startsWith("light."); });',
    '    if (lights.length === 0) { container.textContent = "Nessuna luce trovata."; return; }',
    '    lights.forEach(function(light) {',
    '      var div = document.createElement("div"); div.className = "light-item";',
    '      var cb = document.createElement("input"); cb.type = "checkbox"; cb.value = light.entity_id; cb.id = light.entity_id;',
    '      if (savedConfig.lights && savedConfig.lights.indexOf(light.entity_id) !== -1) { cb.checked = true; }',
    '      var lbl = document.createElement("label"); lbl.htmlFor = light.entity_id; lbl.textContent = (light.attributes.friendly_name || light.entity_id);',
    '      div.appendChild(cb); div.appendChild(lbl); container.appendChild(div);',
    '    });',
    '    var sensorContainer = document.getElementById("sensors-list"); sensorContainer.innerHTML = "";',
    '    var sensors = data.filter(function(e) { return e.entity_id.startsWith("sensor."); });',
    '    if (sensors.length === 0) { sensorContainer.textContent = "Nessun sensore trovato."; }',
    '    else {',
    '      sensors.forEach(function(sensor) {',
    '        var div = document.createElement("div"); div.className = "light-item";',
    '        var rb = document.createElement("input"); rb.type = "radio"; rb.name = "selected_sensor"; rb.value = sensor.entity_id; rb.id = sensor.entity_id;',
    '        if (savedConfig.sensor && savedConfig.sensor === sensor.entity_id) { rb.checked = true; }',
    '        var lbl = document.createElement("label"); lbl.htmlFor = sensor.entity_id; var unit = sensor.attributes.unit_of_measurement || "";',
    '        lbl.textContent = (sensor.attributes.friendly_name || sensor.entity_id) + " (" + sensor.state + unit + ")";',
    '        div.appendChild(rb); div.appendChild(lbl); sensorContainer.appendChild(div);',
    '      });',
    '    }',
    '  })',
    '  .catch(function(err) { console.error("Errore HA Lights:", err); document.getElementById("lights-list").innerHTML = "Errore di connessione!<br><br>Se il Token è corretto, è un problema <b>CORS</b>.<br>Modifica configuration.yaml di Home Assistant:<br><pre style=\'text-align:left; background:#eee; padding:5px; font-size:12px;\'>http:\\n  cors_allowed_origins:\\n    - \\"*\\"</pre><br>Poi riavvia Home Assistant."; });',
    '}',
    'fetchResources();',
    'document.getElementById("sensor-search").addEventListener("input", function() {',
    '  var filter = this.value.toLowerCase();',
    '  var items = document.querySelectorAll("#sensors-list .light-item");',
    '  items.forEach(function(item) { item.style.display = item.textContent.toLowerCase().includes(filter) ? "" : "none"; });',
    '});',
    'document.getElementById("refresh").addEventListener("click", function() { fetchResources(); });',
    'document.getElementById("save").addEventListener("click", function() {',
    '  var btn = document.getElementById("save"); btn.textContent = "Salvataggio..."; btn.disabled = true;',
    '  var selectedLights = []; document.querySelectorAll("#lights-list input:checked").forEach(function(cb) { selectedLights.push(cb.value); });',
    '  var selectedSensor = document.querySelector("input[name=\'selected_sensor\']:checked"); var sensorVal = selectedSensor ? selectedSensor.value : null;',
    '  var nickname = document.getElementById("nickname").value; var urlVal = document.getElementById("ha-url").value.trim(); if(urlVal && !urlVal.startsWith("http")) urlVal = "http://" + urlVal; urlVal = urlVal.replace(/\\/+$/, ""); var tokenVal = document.getElementById("ha-token").value.trim();',
    '  var config = { "lights": selectedLights, "sensor": sensorVal, "nickname": nickname, "haUrl": urlVal, "haToken": tokenVal };',
    '  window.location.href = "pebblejs://close#" + encodeURIComponent(JSON.stringify(config));',
    '});',
    '</script>',
    '</body>',
    '</html>'
  ];
  return html.join('\n');
}

Pebble.addEventListener('showConfiguration', function(e) {
  var lights = JSON.parse(localStorage.getItem('pebble_lights_config') || '[]');
  var sensor = localStorage.getItem('pebble_sensor_config');
  var nickname = localStorage.getItem('pebble_nickname');
  var haUrlConfig = localStorage.getItem('pebble_ha_url');
  var haTokenConfig = localStorage.getItem('pebble_ha_token');
  var options = {
    lights: lights,
    sensor: sensor,
    nickname: nickname,
    haUrl: haUrlConfig,
    haToken: haTokenConfig
  };

  // Avvisa l'orologio che la configurazione è aperta (ferma il timer di chiusura)
  var dict = {};
  dict[KEY_CONFIG_OPEN] = 1;
  Pebble.sendAppMessage(dict, function() {}, function(e) { console.log('Err config open msg'); });

  console.log('Opening config page...');
  Pebble.openURL('data:text/html;charset=utf-8,' + encodeURIComponent(getConfigPage(options)));
});

Pebble.addEventListener('webviewclosed', function(e) {
  console.log('Webview closed. Response: ' + e.response);
  
  // Avvisa l'orologio che la configurazione è chiusa (riavvia il timer)
  var dict = {};
  dict[KEY_CONFIG_OPEN] = 0;
  Pebble.sendAppMessage(dict, function() {}, function(e) { console.log('Err config close msg'); });

  if (e.response) {
    var config = {};
    try {
      config = JSON.parse(decodeURIComponent(e.response));
    } catch (err) {
      console.log('Error parsing config: ' + err);
      return;
    }

    // La data non è più usata, ma salviamo la configurazione delle luci
    if (config.lights) {
      localStorage.setItem('pebble_lights_config', JSON.stringify(config.lights));
    }
    if (config.sensor) {
      localStorage.setItem('pebble_sensor_config', config.sensor);
    }
    if (config.nickname) {
      localStorage.setItem('pebble_nickname', config.nickname);
    }
    if (config.haUrl) {
      var sanitizedUrl = config.haUrl.replace(/\/+$/, "");
      localStorage.setItem('pebble_ha_url', sanitizedUrl);
      haUrl = sanitizedUrl;
    }
    if (config.haToken) {
      localStorage.setItem('pebble_ha_token', config.haToken);
      haToken = config.haToken;
    }

    console.log('Config saved.');
    // Dopo aver salvato, aggiorna lo stato
    updateLights();
    updateSensor();
    sendConfigToPebble();
  }
});
