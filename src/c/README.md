# Menu Hassio for Pebble

**[English below]**

Un'interfaccia di controllo completa per Home Assistant sul tuo smartwatch Pebble. Monitora sensori, controlla le luci, gestisci la musica e chatta con la tua casa, tutto dal tuo polso con un'interfaccia ad alto contrasto e leggibile.

---

## 🇮🇹 Italiano

**Menu Hassio** trasforma il tuo Pebble in un telecomando potente per la tua Smart Home.

### Funzionalità Principali
*   **Monitoraggio Sensori:** Visualizza un sensore a scelta (es. percentuale batteria auto, consumi casa) direttamente nella schermata principale con caratteri grandi e leggibili.
*   **Controllo Luci:** Menu rapido per accendere e spegnere le luci selezionate.
*   **Chat Home Assistant:** Invia e ricevi messaggi di testo verso Home Assistant (richiede `input_text`). Supporta la dettatura vocale (su modelli con microfono) e messaggi predefiniti.
*   **Controllo Musica:** Gestisci i media player attivi (Play/Pausa, Volume, Traccia successiva/precedente).
*   **Design Accessibile:** Interfaccia nera su bianco ad alto contrasto per la massima visibilità.

### Requisiti Home Assistant
Per sfruttare tutte le funzionalità, è necessario aggiungere questa configurazione al tuo `configuration.yaml` o creare gli helper via interfaccia:

1.  **Chat:** Crea un'entità `input_text` chiamata `pebble_chat`.
    ```yaml
    input_text:
      pebble_chat:
        name: Pebble Chat
        initial: ""
        max: 255
    ```

2.  **CORS (Importante per la configurazione):** Per permettere alla pagina di configurazione dell'app Pebble di caricare la lista delle tue luci, devi abilitare il CORS:
    ```yaml
    http:
      cors_allowed_origins:
        - "*"
    ```

### Configurazione App
1.  Apri le impostazioni dell'app sull'app Pebble del telefono.
2.  Inserisci l'**URL** del tuo Home Assistant (es. `https://tua-casa.duckdns.org` o IP locale se sei in WiFi).
3.  Inserisci un **Token a Lunga Durata** (crealo dal tuo profilo utente in HA -> Token di accesso a lunga durata).
4.  Inserisci il tuo **Nickname** per la chat.
5.  Seleziona le **Luci** che vuoi controllare e il **Sensore** da mostrare nella home.

### Controlli
**Schermata Principale:**
*   **SU:** Apre il controllo Musica.
*   **SELEZIONA:** Apre il menu Luci.
*   **GIU:** Apre la Chat.

**Menu Luci:**
*   **SU/GIU:** Scorri le luci.
*   **SELEZIONA:** Accendi/Spegni (Toggle).
*   **SHAKE (Scuoti il polso):** Accendi/Spegni la luce selezionata.

**Musica:**
*   **SU/GIU (Click):** Traccia Precedente / Successiva.
*   **SU/GIU (Pressione Lunga):** Volume Su / Giù.
*   **SELEZIONA:** Play / Pausa.

---

## 🇬🇧 English

**Menu Hassio** turns your Pebble into a powerful remote for your Smart Home.

### Key Features
*   **Sensor Monitoring:** Display a specific sensor (e.g., car battery, power usage) directly on the main screen with large, readable fonts.
*   **Light Control:** Quick menu to toggle selected lights on and off.
*   **Home Assistant Chat:** Send and receive text messages to Home Assistant (requires `input_text`). Supports voice dictation (on microphone-enabled models) and canned messages.
*   **Music Control:** Control active media players (Play/Pause, Volume, Next/Prev Track).
*   **Accessible Design:** High-contrast black-on-white interface for maximum visibility.

### Home Assistant Requirements
To use all features, add the following to your `configuration.yaml` or create the helpers via UI:

1.  **Chat:** Create an `input_text` entity named `pebble_chat`.
    ```yaml
    input_text:
      pebble_chat:
        name: Pebble Chat
        initial: ""
        max: 255
    ```

2.  **CORS (Important for Configuration):** To allow the Pebble app configuration page to fetch your lights list, you must enable CORS:
    ```yaml
    http:
      cors_allowed_origins:
        - "*"
    ```

### App Configuration
1.  Open the app settings within the Pebble mobile app.
2.  Enter your Home Assistant **URL** (e.g., `https://your-home.duckdns.org` or local IP if on WiFi).
3.  Enter a **Long-Lived Access Token** (create one in your HA User Profile -> Long-Lived Access Tokens).
4.  Enter your **Nickname** for the chat.
5.  Select the **Lights** you want to control and the **Sensor** to display on the home screen.

### Controls
**Main Screen:**
*   **UP:** Open Music Control.
*   **SELECT:** Open Lights Menu.
*   **DOWN:** Open Chat.

**Lights Menu:**
*   **UP/DOWN:** Scroll through lights.
*   **SELECT:** Toggle On/Off.
*   **SHAKE (Wrist):** Toggle the currently selected light.

**Music:**
*   **UP/DOWN (Click):** Previous / Next Track.
*   **UP/DOWN (Long Press):** Volume Up / Down.
*   **SELECT:** Play / Pause.

---

**Author:** Igino
**License:** MIT
