# Gumball Machine

This is a ESP32 and ESP8266 client that connects to a Web Socket server for the purpose of acting as a Gumball machine dispenser. A companion UI application is located in the same repo.

## Deploying

Assuming the code has been flashed to the board, the board will start up and attempt to connect to a previously configured WIFI Access Point

If no access point has been found within 20 seconds it will go into configuration mode. During configuraiton mode the board will advertise a unique AP name with no password. You can connect to this AP and configure the AP name and PW you wish the board to connect to.

If no connection has been made to the temporary AP within 30 seconds it will revert to the connection cycle and start again (no reboot).

### Web Server

The board will start a web server and advertise this via mDNS so that it may be discovered on the network

Using an application like "discover" on OSX to see advertised mDNS application or using the following command

```bash
$ dns-sd -B
```

You should expect output like:

```bash
(base) ➜  ~ dns-sd -B
Browsing for _http._tcp
DATE: ---Fri 29 Apr 2022---
14:25:49.759  ...STARTING...
Timestamp     A/R    Flags  if Domain               Service Type         Instance Name
14:25:49.762  Add        3  14 local.               _http._tcp.          DiskStation
14:25:49.762  Add        3  14 local.               _http._tcp.          Twilio_Gumball_80842178
14:25:49.762  Add        3  14 local.               _http._tcp.          shellydimmer2-DEADBEEF9CF7
```

You can then get the IP address with:

```bash
dns-sd -Gv4v6 Twilio_Gumball_80842178.local
```

And expect the output like:

```
(base) ➜  ~ dns-sd -Gv4v6 Twilio_Gumball_80842178.local
DATE: ---Fri 29 Apr 2022---
14:28:38.629  ...STARTING...
Timestamp     A/R    Flags if Hostname                               Address                                      TTL
14:28:38.838  Add        2 14 Twilio_Gumball_80842178.local.         192.168.1.197                                120
```

Note that adding _.local_ to the end of the hostname is required

### Web Socket Connection

Once connected to WIFI the baord will attempt to establish a WebSocket with the configured host. Upon disconnection it will try to connect again continuously.

#### Dispense

If the board receives a message that can be parsed as JSON with the following payload it will trigger the motor pin for a period of time

```json
{ "action": "dispense" }
```

or

```json
{ "action": "dispense", "duration": 10000 }
```

Note the dispense duration is in milliseconds

#### Ping/Pong

To keep the WS connection alive the board will response to ping messages in JSON format with a respective PONG, this is _not_ using the WebSocket ping/pong protocol but rather at the application level messages.

The board expects

```json
{ "action": "ping" }
```

and will respond with

```json
{ "action": "pong" }
```

### Updating code - Over the air updates (OTA)

New firmware and file systems may be updated by navigating to http://<ip address>/update

Note: It's recommended to change the OTA username and password as required

### To Heroku

To deploy the application to Heroku, first have the project linked as a report repo, then run
`$ git push heroku main`

This will cause Heroku to run the build commands in the `package.json` and deploy to a live environment

## Example incoming messages

The app server can be triggered by one of the following inbound requests
