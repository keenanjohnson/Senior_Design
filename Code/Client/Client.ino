/*
  Arduino Client Node
 
 Client program to run Arduino + Ethernet shield plugged into PLN device
 via Ethernet cable communicating to a PLN device also connected to the
 router then to the basestation on the other side.
 
 Circuit:
 * Ethernet shield attached to pins 10, 11, 12, 13
 
 */

#include <SPI.h>
#include <Ethernet.h>




/*------------------------------------------------------------------------
CONSTANTS/TYPES
------------------------------------------------------------------------*/
//////////////////////////////////////////////////////////////////////////
// KEEP TWO SKETCHES IN SYNC BECAUSE ARDUINO'S IDE IS DUMB ///////////////
//////////////////////////////////////////////////////////////////////////
#define STATIC_ASSERT(COND,MSG) typedef char static_assertion_##MSG[(COND)?1:-1]

#define PACKET_WAIT_TIMEOUT 1000

typedef byte node_cmds; enum {
  NODE_OFF = 0,
  NODE_ON,
  NODE_SAVE_ID,
  NODE_KEEP_ALIVE,
  NODE_STATUS,
  // add new cmds here

  NODE_CMDS_CNT
};

const char *node_cmds_string[] =
{   
  "OFF\0",
  "ON\0",
  "SAVE ID\0",
  "KEEP ALIVE\0",
  "STATUS\0",
};
STATIC_ASSERT( sizeof( node_cmds_string ) / sizeof( char* ) == NODE_CMDS_CNT, make_arrays_same_size );

typedef struct __node_packet {
  byte id;
  node_cmds cmd;
  byte data;
} node_packet;
//////////////////////////////////////////////////////////////////////////

#define NODE_RELAY_PIN   2

/*------------------------------------------------------------------------
VARIABLES
------------------------------------------------------------------------*/
// Enter a MAC address for your controller below.
// Newer Ethernet shields have a MAC address printed on a sticker on the shield
byte mac[] = { 0x00, 0xAA, 0xBB, 0xCC, 0xDE, 0x25 };

// Initialize the Ethernet client library
// with the IP address and port of the server
// that you want to connect to (port 80 is default for HTTP):
EthernetClient client;

// An EthernetUDP instance to let us send and receive packets over UDP
EthernetUDP UDP;
unsigned int localPort = 8888;      // local port to listen on

// UDP IP broadcast
char packetBuffer[UDP_TX_PACKET_MAX_SIZE]; //buffer to hold incoming packet,
boolean IP_found = false;

// storage for node's ID grabbed from server
int node_id;

// loop count to determine when to send NODE_KEEP_ALIVE
unsigned long loop_time;
boolean keep_alive_rec;
boolean keep_alive_sent;

/*------------------------------------------------------------------------
FUNCTION PROTOTYPES
------------------------------------------------------------------------*/
void print_local_ip_address();
void process_cmd_packet( const node_packet &pkt );
int send_cmd_to_server( const node_cmds &cmd, const byte &data, EthernetClient* client );

/*------------------------------------------------------------------------
SETUP
------------------------------------------------------------------------*/
void setup() {
  // Open serial communications and wait for port to open:
  Serial.begin(9600);

  // setup pins
  pinMode(NODE_RELAY_PIN, OUTPUT);

  // start the Ethernet connection:
  while(Ethernet.begin(mac) == 0) {
    Serial.println("Failed to configure Ethernet using DHCP");
    // Wait a bit and try again
    delay(100);
  }

  // Start listening on UDP port
  UDP.begin(localPort);

  // print local IP address
  print_local_ip_address();

  // disconnect init
  keep_alive_rec = false;
  keep_alive_sent = false;
}

/*------------------------------------------------------------------------
LOOP
------------------------------------------------------------------------*/
void loop() {
    // if there's data available, read a packet
  int packetSize = UDP.parsePacket();

  if(packetSize && !IP_found)
  {
    Serial.print("Received packet of size ");
    Serial.println(packetSize);
    Serial.print("From ");
    IPAddress remote = UDP.remoteIP();
    for (int i =0; i < 4; i++)
    {
      Serial.print(remote[i], DEC);
      if (i < 3)
      {
        Serial.print(".");
      }
    }
    Serial.print(", port ");
    Serial.println(UDP.remotePort());

    // read the packet into packetBufffer
    UDP.read(packetBuffer,UDP_TX_PACKET_MAX_SIZE);
    Serial.println("Contents:");
    Serial.println(packetBuffer);

    IP_found = true;

    while( !client.connect( UDP.remoteIP(), 11700) ) {
      Serial.println("Client cannot connect...");
      delay(100);
    }

    while( !client.connected() ) {
      Serial.println("Waiting to connect...");
    }

    // bring up connection with KEEP_ALIVE cmd
    send_cmd_to_server( NODE_KEEP_ALIVE, digitalRead( NODE_RELAY_PIN ), &client );

    Serial.println("CONN BROUGHT UP");
  }

  if( IP_found )
  {
    boolean data_read = false;
    int data_timeout = 0;
    node_packet packet;

    if( client.available() >= sizeof( node_packet ) ) {
      data_read = true;

      client.read( (byte*)&packet, sizeof( node_packet ) );
    }

    if( data_read ) {
      if( packet.cmd != NODE_KEEP_ALIVE || 1 ) {
        Serial.print("RECEIVED --- ");
        Serial.print("ID: "); Serial.print( packet.id );
        Serial.print(" CMD: "); Serial.print( node_cmds_string[ packet.cmd ] );
        Serial.print(" Data: "); Serial.print( packet.data, HEX );
        Serial.println();
      }

      process_cmd_packet( packet );
    }

    if( millis() - loop_time > PACKET_WAIT_TIMEOUT ) {
      // keep alive sent and didn't receive a reply
      // close connection to server, reset checks
      if( keep_alive_sent && !keep_alive_rec ) {
        Serial.println("TIMEOUT disconnection...");
        IP_found = false;
        client.flush();
        client.stop();
        keep_alive_sent = false;
        keep_alive_rec = true;
      } else {
        send_cmd_to_server( NODE_KEEP_ALIVE, digitalRead( NODE_RELAY_PIN ), &client );
      }
    }

  } else {
    Serial.print("IP NOT FOUND");
    Serial.println();
  }
}

/*------------------------------------------------------------------------
FUNCTION DEFINITIONS
------------------------------------------------------------------------*/
void print_local_ip_address()
{
  Serial.print("My IP address: ");
  for (byte thisByte = 0; thisByte < 4; thisByte++) {
    // print the value of each byte of the IP address:
    Serial.print(Ethernet.localIP()[thisByte], DEC);
    Serial.print(".");
  }
  Serial.println();
}

void process_cmd_packet( const node_packet &pkt )
{
  loop_time = millis();

  switch( pkt.cmd ) {
    case NODE_OFF:
      digitalWrite(NODE_RELAY_PIN, LOW);
      send_cmd_to_server(NODE_STATUS, digitalRead( NODE_RELAY_PIN ), &client);
      break;
    case NODE_ON:
      digitalWrite(NODE_RELAY_PIN, HIGH);
      send_cmd_to_server(NODE_STATUS, digitalRead( NODE_RELAY_PIN ), &client);
      break;
    case NODE_SAVE_ID:
      keep_alive_rec = true;
      node_id = pkt.data;
      break;
    case NODE_KEEP_ALIVE:
      keep_alive_rec = true;
      break;
    case NODE_STATUS:
      Serial.println("Returning status...");
      send_cmd_to_server(NODE_STATUS, digitalRead( NODE_RELAY_PIN ), &client);
      break;
    default:
      Serial.println("CMD NOT KNOWN");
      break;
  }
}

int send_cmd_to_server( const node_cmds &cmd, const byte &data, EthernetClient* client )
{
  node_packet packet;

  packet.id = node_id;
  packet.cmd = cmd;
  packet.data = data;

  if( cmd == NODE_KEEP_ALIVE ) {
    keep_alive_rec = false;
    keep_alive_sent = true;
    loop_time = millis();
  }

  if( cmd != NODE_KEEP_ALIVE || 1 ) {
    Serial.print("SENDING --- ");
    Serial.print("ID: "); Serial.print( packet.id );
    Serial.print(" CMD: "); Serial.print( node_cmds_string[ packet.cmd ] );
    Serial.print(" Data: "); Serial.print( packet.data, HEX );
    Serial.println();
  }

  return (*client).write( (byte*)&packet, sizeof( node_packet ) );
}
