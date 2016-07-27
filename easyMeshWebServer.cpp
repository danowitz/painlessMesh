#include <Arduino.h>

extern "C" {
#include "user_interface.h"
#include "espconn.h"
}

#include "easyMeshWebServer.h"
#include "FS.h"

espconn webServerConn;
esp_tcp webServerTcp;


//***********************************************************************
void webServerInit( void ) {
    webServerConn.type = ESPCONN_TCP;
    webServerConn.state = ESPCONN_NONE;
    webServerConn.proto.tcp = &webServerTcp;
    webServerConn.proto.tcp->local_port = WEB_PORT;
    espconn_regist_connectcb(&webServerConn, webServerConnectCb);
    sint8 ret = espconn_accept(&webServerConn);
    
    SPIFFS.begin(); // start file system for webserver
    
    if ( ret == 0 )
        Serial.printf("web server established on port %d\n", WEB_PORT );
    else
        Serial.printf("web server on port %d FAILED ret=%d\n", WEB_PORT, ret);
    
    return;
}

//***********************************************************************
void webServerConnectCb(void *arg) {
  struct espconn *newConn = (espconn *)arg;
//  webConnections.push_back( newConn );

//  Serial.printf("web Server received connection !!!\n");

  espconn_regist_recvcb(newConn, webServerRecvCb);
  espconn_regist_sentcb(newConn, webServerSentCb);
  espconn_regist_reconcb(newConn, webServerReconCb);
  espconn_regist_disconcb(newConn, webServerDisconCb);
}

/***********************************************************************/
void webServerRecvCb(void *arg, char *data, unsigned short length) {
  //received some data from webServer connection
  String request( data );
  
//  Serial.printf("In webServer_server_recv_cb count=%d\n", webCount);
  struct espconn *activeConn = (espconn *)arg;
//  Serial.printf("webServer recv"); //--->%s<----\n", request.c_str());

  String get("GET ");
  String path; 

  if ( request.startsWith( get ) ) {
    uint16_t endFileIndex = request.indexOf(" HTTP");
    path = request.substring( get.length(), endFileIndex );
    if( path.equals("/") )
      path = "/index.html";
  }

  String msg = "";
  char ch;

  File f = SPIFFS.open( path, "r" );
  if ( !f ) {
    msg = "File-->" + path + "<-- not found\n";
  }
  else {
//  Serial.printf("path=%s\n", path.c_str() );
    while ( f.available() ) {
      ch = f.read();
      msg.concat( ch );
    }
  }

  //Serial.printf("msg=%s<---\n", msg.c_str() );
    
  espconn_send(activeConn, (uint8*)msg.c_str(), msg.length());
}

/***********************************************************************/
void webServerSentCb(void *arg) {
  //data sent successfully
//  Serial.printf("webServer sent cb \r\n");
  struct espconn *requestconn = (espconn *)arg;
  espconn_disconnect( requestconn );
}

/***********************************************************************/
void webServerDisconCb(void *arg) {
//  Serial.printf("In webServer_server_discon_cb\n");
}

/***********************************************************************/
void webServerReconCb(void *arg, sint8 err) {
//  Serial.printf("In webServer_server_recon_cb err=%d\n", err );
}







