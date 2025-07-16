# External-C2
External C2 is a specification to allow third-party programs to act as a communication layer for Cobalt Strike’s Beacon payload.

## Overview

### What is External Command and Control?

Cobalt Strike’s External Command and Control (External C2) interface allows thirdparty programs to act as a communication layer between Cobalt Strike and its Beacon payload.

### Architecture

The External C2 system consists of a third-party controller, a third-party client, and the External C2 service provided by Cobalt Strike. The third-party client and thirdparty servers are components external to Cobalt Strike. The third-party may develop these components in a language of their choosing.

<img width="3258" height="2161" alt="ExtC2-diagram" src="https://github.com/user-attachments/assets/e982fd7c-7072-405c-8a4a-b51c941894be" />

The third-party controller connects to Cobalt Strike’s External C2 service. This service serves a payload stage, sends tasks for, and receives responses from a Beacon session. The third-party client injects a Beacon payload stage into memory, reads responses from a Beacon session, and sends tasks to it. The third-party controller and third-party client communicate the payload stage, tasks, and responses to each other.

## External C2 Protocol
### Frames

The External C2 server and the SMB Beacon use the same format for their frames. All frames start with a 4-byte little-endian byte order integer. This integer is the length of the data within the frame. The frame data always follows this length value.

All communication to and from the External C2 server uses this frame format. All communication to and from the SMB Beacon named pipe server uses this frame format as well.

> [!NOTE]
> many high-level languages will use big-endian (also called network-byte) order when they serialize an integer to a stream. Developers should make sure they get this byte order correct when they build their third-party controller and client programs. The 4-byte little-endian byte order is what the SMB Beacon uses to allow another Beacon (and now third-party client) to control it. The External C2 server uses this frame convention to stay consistent with the SMB Beacon.

### No authentication

The External C2 server does not authenticate the third-party controllers that connect to it. This might sounds scary. It isn’t. The External C2 server exists to serve payload stages, receive metadata, serve tasks, and receive responses from Beacon sessions. These are the same services that Cobalt Strike’s other listeners (HTTP, DNS, etc.) provide.

## External C2 Components

### External C2 Server

The ```&externalc2_start``` function in Aggressor Script starts the External C2 server.

```perl
# start the External C2 server and bind to 0.0.0.0:2222
externalc2_start(“0.0.0.0”, 2222);
```

### Third-party Client Controller

When a new session is desired, the third-party controller connects to the External C2 server. Each connection to the External C2 server services one session.

The third-party controller’s first job is to configure and request a payload stage. 

To configure the payload stage, the controller may write one or more frames that contain a ```key=value string```. These frames will populate options for the session. The External C2 server does not acknowledge these frames.

| Option   | Default | Description                                                                                 |
|----------|---------|---------------------------------------------------------------------------------------------|
| arch     | x86     | The architecture of the payload stage. Acceptable options: x86, x64                        |
| pipename |         | The named pipe name                                                                         |
| block    | 100     | A time in milliseconds that indicates how long the External C2 server should block when no new tasks are available. Once this time expires, the External C2 server will generate a no-op frame. |

Once all options are sent, the third-party controller writes a frame that consists of the string ```go```. This tells the External C2 server to send the payload stage.

The third-party controller reads the payload stage and relays it to the third-party client.

At this point, the third-party controller must wait to receive a frame from the thirdparty client. When this frame does come, the third-party controller must write the frame to the connection it made to the External C2 server.

The third-party controller must now read a frame from the External C2 server. The External C2 server will wait, up to the configured block time, for tasks to become available. If no task is available, the External C2 server will generate a frame with an empty task. The third-party controller must send the frame it reads to the thirdparty client.

The third-party controller then repeats this process: wait for a frame from the thirdparty client; write it to the External C2 server connection. Read a frame from the External C2 server connection; write this frame to the third-party client. Rinse, lather, and repeat.

Cobalt Strike will mark the Beacon session as dead when the third-party controller disconnects from the External C2 server. There is no capability to resume sessions.

### Third-party Client

The third-party client should receive a Beacon payload stage from the third-party controller. The payload stage is a Reflective DLL with its header patched to make it self-bootstrapping. Normal process injection techniques will work to run this payload stage.

Once the payload stage is running, the third-party client should connect to its named pipe server. The third-party client may open the named pipe server as a file with read write mode. The file is ```\\.\pipe\[pipe name here]```. If the third-party client language/runtime has APIs for named pipes, it’s fine to use those too.

The third-party client must now read a frame from the Beacon named pipe connection. Once this frame is read, the third-party client must relay this frame to the third-party controller to process.

The third-party client must now wait for a frame from the third-party controller. Once this frame is available, the third-party client must write this frame to the named pipe connection.

The rest of the External C2 life cycle is a repeat of these steps. Read a frame from the named pipe connection. Send this frame to the third-party controller. Wait for a frame from the third-party controller. Write this frame to the named pipe connection. So on and so forth.

<table>
  <thead>
    <tr>
      <th>Step</th>
      <th>External C2</th>
      <th>Controller</th>
      <th>Client</th>
      <th>SMB Beacon</th>
    </tr>
  </thead>
  <tbody>
    <tr><td>1</td><td></td><td></td><td>Request new session from controller</td><td></td></tr>
    <tr><td>2</td><td></td><td>Connect to External C2</td><td></td><td></td></tr>
    <tr><td>3</td><td></td><td>← Send options</td><td></td><td></td></tr>
    <tr><td>4</td><td></td><td>← Request stage</td><td></td><td></td></tr>
    <tr><td>5</td><td>Send stage →</td><td></td><td></td><td></td></tr>
    <tr><td>6</td><td></td><td>Relay stage →</td><td></td><td></td></tr>
    <tr><td>7</td><td></td><td></td><td>Inject stage into a process</td><td></td></tr>
    <tr><td>8</td><td></td><td></td><td></td><td>Start named pipe server</td></tr>
    <tr><td>9</td><td></td><td></td><td>Connect to named pipe server</td><td></td></tr>
    <tr><td>10</td><td></td><td></td><td></td><td>← Write metadata</td></tr>
    <tr><td>11</td><td></td><td>← Relay metadata</td><td></td><td></td></tr>
    <tr><td>12</td><td>← Relay metadata</td><td></td><td></td><td></td></tr>
    <tr><td>13</td><td>Process metadata</td><td></td><td></td><td></td></tr>
    <tr>
      <td colspan="5"><em>A new Beacon session appears within Cobalt Strike</em></td>
    </tr>
    <tr><td>14</td><td>User tasks session or empty task made</td><td></td><td></td><td></td></tr>
    <tr><td>15</td><td>Write tasks →</td><td></td><td></td><td></td></tr>
    <tr><td>16</td><td></td><td></td><td>Relay tasks →</td><td></td></tr>
    <tr><td>17</td><td></td><td></td><td></td><td>Relay tasks →</td></tr>
    <tr><td>18</td><td></td><td></td><td>Process tasks</td><td></td></tr>
    <tr><td>19</td><td>← Write response</td><td></td><td></td><td></td></tr>
    <tr><td>20</td><td>← Relay response</td><td></td><td></td><td></td></tr>
    <tr><td>21</td><td>← Relay response</td><td></td><td></td><td></td></tr>
    <tr><td>22</td><td></td><td></td><td>Process response</td><td></td></tr>
    <tr>
      <td colspan="5"><em>Repeat steps 14–22 while session is alive</em></td>
    </tr>
    <tr><td>24</td><td></td><td></td><td></td><td>Session exits</td></tr>
    <tr><td>25</td><td></td><td></td><td>Error when reading or writing to named pipe 😞</td><td></td></tr>
    <tr><td>26</td><td></td><td></td><td>← Notify controller</td><td></td></tr>
    <tr><td>27</td><td>Disconnect from External C2</td><td>exit</td><td></td><td></td></tr>
    <tr>
      <td colspan="5"><em>Session marked as dead within Cobalt Strike</em></td>
    </tr>
  </tbody>
</table>

## Setup

1. Make sure that Mingw-w64 (including mingw-w64-binutils) has been installed.
2. Run the build command:

```
i686-w64-mingw32-gcc extc2example.c -o example.exe -lws2_32
```

