# BitTorrent Protocol

## 1. Tracker

First of all, we exchange information with all clients. We receive details about all the files in the network: who owns them, how many segments they have, and their hashes.

After the initial phase, we enter an infinite loop where we wait for requests from clients. These requests can be:

- a request for information about a file from a client who wants to download it (the list of seeders, the number of segments, and their hashes);
- an update to the list of seeders every 10 segments downloaded;
- a final signal sent by a client's download thread when it has finished downloading a file.

When all files have been downloaded by all clients, the tracker sends a final signal to the upload threads of the clients to allow them to stop, and then it also finishes its execution.

## 2. Client

In the initial phase, the client reads its input file and gathers information about the files it owns, then connects to the tracker and sends this information.

After the initial phase, the client waits for a signal from the tracker that it can begin execution. Once it receives the signal, the client splits into two threads: one for downloading and one for uploading.

### a) Download

The client goes through each of the files it wants to download. It sends an initial request to the tracker to receive the necessary information for the download. After receiving the response, it begins to request segments from seeders randomly selected from the list. To avoid overloading a seeder, two consecutive requests are never sent to the same seeder.

Depending on the seeder's response, the client marks the segment as downloaded or continues sending requests for it. After finishing the download of all files, the client sends a final signal to the tracker.

### b) Upload

In the upload thread, the client waits for requests from other clients. For each request, it checks whether it has the requested segment and responds accordingly.

This thread terminates when it receives the final signal from the tracker.

## Additional Details

- A mutex was used to protect access to the list of hashes for downloaded segments.
- A different tag was used for each `send-recv` pair, since messages with the same data type sent consecutively would often get mixed up.
