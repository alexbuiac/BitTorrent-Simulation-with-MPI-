#include <mpi.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>
#include <map>
#include <iostream>
#include <fstream>

using namespace std;

#define TRACKER_RANK 0
#define MAX_FILES 10
#define MAX_FILENAME 15
#define HASH_SIZE 32
#define MAX_CHUNKS 100

#define INITIAL_REQUEST 100
#define SWARN_UPDATE 101
#define FINISHED_DOWNLOADING 102

typedef struct {
    int rank;
    int num_files_had;
    int num_files_wanted;
    map<string, pair<int, vector<string>>> files_had;
    vector<string> files_wanted;
    map<string, vector<bool>> downloaded_chunks;
    map<string, vector<string>> hashes;

    pthread_mutex_t mutex;
} in_data;

// check if all chunks are downloaded
bool download_finished(vector<bool> &downloaded_chunks) {
    for (bool chunk : downloaded_chunks) {
        if (!chunk)
            return false;
    }
    return true;
}

// download thread function
void *download_thread_func(void *arg)
{   
    in_data *data = (in_data *)arg;

    // set seed for random
    srand(time(NULL));

    // download files
    int request_tag;
    for (string filename : data->files_wanted) {
        // send request to download
        request_tag = INITIAL_REQUEST;
        MPI_Send(&request_tag, 1, MPI_INT, TRACKER_RANK, 5, MPI_COMM_WORLD);
        
        // send filename
        MPI_Send(filename.c_str(), MAX_FILENAME, MPI_CHAR, TRACKER_RANK, 6, MPI_COMM_WORLD);

        // recv num of chunks
        int num_chunks;
        MPI_Recv(&num_chunks, 1, MPI_INT, TRACKER_RANK, 7, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

        // recv chunks
        vector<string> chunks;
        for (int i = 0; i < num_chunks; i++) {
            char chunk[HASH_SIZE + 1];
            memset(chunk, 0, HASH_SIZE + 1);

            MPI_Recv(chunk, HASH_SIZE, MPI_CHAR, TRACKER_RANK, 8, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            chunks.push_back(string(chunk));
        }

        // recv peers list
        vector<int> peers;
        int num_peers;
        MPI_Recv(&num_peers, 1, MPI_INT, TRACKER_RANK, 9, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        peers.resize(num_peers);
        MPI_Recv(peers.data(), num_peers, MPI_INT, TRACKER_RANK, 10, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        
        // vector for keeping track of downlaods
        data->downloaded_chunks[filename] = vector<bool>(num_chunks, false);

        // initializing hashes vector
        pthread_mutex_lock(&data->mutex);
        if (data->hashes.find(filename) == data->hashes.end()) {
            data->hashes[filename] = vector<string>();
        }
        pthread_mutex_unlock(&data->mutex);


        // get chunks
        for (int i = 0; i < num_chunks; i++) {
            // swarn update
            if (i % 10 == 0) {
                // send request for swarm update
                request_tag = SWARN_UPDATE;
                MPI_Send(&request_tag, 1, MPI_INT, TRACKER_RANK, 5, MPI_COMM_WORLD);
                MPI_Send(filename.c_str(), MAX_FILENAME, MPI_CHAR, TRACKER_RANK, 11, MPI_COMM_WORLD);

                // recv swarm
                MPI_Recv(&num_peers, 1, MPI_INT, TRACKER_RANK, 12, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                peers.resize(num_peers);
                MPI_Recv(peers.data(), num_peers, MPI_INT, TRACKER_RANK, 13, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            }
            
            // send request for chunk until we get it
            int last_peer_idx = -1;
            while (!data->downloaded_chunks[filename][i]) {
                // get random peer index
                int peer_index = rand() % num_peers;
                
                // don't send a request to ourselves or the last peer 
                while (peers[peer_index] == data->rank || peer_index == last_peer_idx)
                    peer_index = rand() % num_peers;
                last_peer_idx = peer_index;
                
                // send request for chunk
                int req = 1;
                MPI_Send(&req, 1, MPI_INT, peers[peer_index], 17, MPI_COMM_WORLD);
                MPI_Send(filename.c_str(), MAX_FILENAME, MPI_CHAR, peers[peer_index], 20, MPI_COMM_WORLD);
                MPI_Send(chunks[i].c_str(), HASH_SIZE, MPI_CHAR, peers[peer_index], 21, MPI_COMM_WORLD);

                // recv answer
                int answer;
                MPI_Recv(&answer, 1, MPI_INT, peers[peer_index], 16, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

                // if peer has the chunk, mark it as downloaded
                if (answer) {
                    data->downloaded_chunks[filename][i] = true;

                    // add chunk to hashes
                    pthread_mutex_lock(&data->mutex);
                    data->hashes[filename].push_back(chunks[i]);
                    pthread_mutex_unlock(&data->mutex);
                }
            }
        }

        // open output file
        string out_filename = "client" + to_string(data->rank) + "_" + filename;
        ofstream fout(out_filename);

        if (!fout.is_open()) {
            printf("Rank %d: Error on opening output file\n", data->rank);
            exit(-1);
        }

        // write hashes to file
        pthread_mutex_lock(&data->mutex);
        for (int i = 0; i < num_chunks; i++)
            fout << data->hashes[filename][i] << endl;
        pthread_mutex_unlock(&data->mutex);

        // close file
        fout.close();
    }

    // send finished signal
    request_tag = FINISHED_DOWNLOADING;
    MPI_Send(&request_tag, 1, MPI_INT, TRACKER_RANK, 5, MPI_COMM_WORLD);

    // exit thread
    pthread_exit(NULL);
}

// check if chunk is in hashes
int check_chunk(string filename, string hash, in_data *data) {
    for (string it : data->hashes[filename]) {
        if (it == hash) {
            return 1;
        }
    }
    return 0;
}

// upload thread function
void *upload_thread_func(void *arg)
{
    in_data *data = (in_data *)arg;

    // wait for requests
    while (true) {
        MPI_Status status;
        int request_tag;
        MPI_Recv(&request_tag, 1, MPI_INT, MPI_ANY_SOURCE, 17, MPI_COMM_WORLD, &status);
        
        // exit if tracker sends finish signal
        if (request_tag == 0) {
            break;
        }

        // recv filename and hash
        char filename_c[MAX_FILENAME + 1];
        memset(filename_c, 0, MAX_FILENAME + 1);

        char hash_c[HASH_SIZE + 1];
        memset(hash_c, 0, HASH_SIZE + 1);

        MPI_Recv(filename_c, MAX_FILENAME, MPI_CHAR, MPI_ANY_SOURCE, 20, MPI_COMM_WORLD, &status);
        MPI_Recv(hash_c, HASH_SIZE, MPI_CHAR, status.MPI_SOURCE, 21, MPI_COMM_WORLD, &status);

        // check if chunk is in hashes
        pthread_mutex_lock(&data->mutex);
        int answer = check_chunk(string(filename_c), string(hash_c), data);
        pthread_mutex_unlock(&data->mutex);

        // send answer
        MPI_Send(&answer, 1, MPI_INT, status.MPI_SOURCE, 16, MPI_COMM_WORLD);
    }

    // exit thread
    pthread_exit(NULL);
}

// check if all peers are done
bool all_done(vector<bool> peers_done) {
    for (bool done : peers_done) {
        if (!done)
            return false;
    }
    return true;
}

// tracker function
void tracker(int numtasks, int rank) {
    map<string, tuple<vector<int>, int, vector<string>>> files;

    // recv info from peers
    for (int i = 1; i < numtasks; i++) {
        // recv number of files
        int num_files_had;
        MPI_Recv(&num_files_had, 1, MPI_INT, i, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

        // recv files
        for (int j = 0; j < num_files_had; j++) {
            // recv filename
            char filename_c[MAX_FILENAME + 1];
            memset(filename_c, 0, MAX_FILENAME + 1);
            MPI_Recv(filename_c, MAX_FILENAME, MPI_CHAR, i, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            string filename = string(filename_c);

            // check if we already have the file
            bool file_exists = true;
            if (files.find(filename) == files.end()) {
                file_exists = false;
            }

            // add peer
            if (file_exists) {
                vector<int> &peers = get<0>(files[filename]);
                peers.push_back(i);
            }

            // recv chunks
            int num_chunks;
            MPI_Recv(&num_chunks, 1, MPI_INT, i, 2, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            vector<string> chunks;
            for (int k = 0; k < num_chunks; k++) {
                char chunk[HASH_SIZE + 1];
                memset(chunk, 0, HASH_SIZE + 1);
                MPI_Recv(chunk, HASH_SIZE, MPI_CHAR, i, 3, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

                chunks.push_back(string(chunk));
            }
            
            // add file
            if (!file_exists) {    
                vector<int> peers;
                peers.push_back(i);
                files[filename] = make_tuple(peers, num_chunks, chunks);
            }
        }
    }

    // send start signal to peers
    int start_signal = 1234;
    for (int i = 1; i < numtasks; i++) {
        MPI_Send(&start_signal, 1, MPI_INT, i, 4, MPI_COMM_WORLD);
    }

    vector<bool> peers_done(numtasks, false);
    peers_done[0] = true;

    // wait for requests
    int request_tag;
    while (true) {
        MPI_Status status;
        MPI_Recv(&request_tag, 1, MPI_INT, MPI_ANY_SOURCE, 5, MPI_COMM_WORLD, &status);

        // initial file request
        if (request_tag == INITIAL_REQUEST) {
            char filename[MAX_FILENAME + 1];
            memset(filename, 0, MAX_FILENAME + 1);
            MPI_Recv(filename, MAX_FILENAME, MPI_CHAR, status.MPI_SOURCE, 6, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            // send number of chunks
            int num_chunks = get<1>(files[string(filename)]);
            MPI_Send(&num_chunks, 1, MPI_INT, status.MPI_SOURCE, 7, MPI_COMM_WORLD);

            // send chunks
            vector<string> &chunks = get<2>(files[string(filename)]);
            for (int i = 0; i < num_chunks; i++) {
                MPI_Send(chunks[i].c_str(), HASH_SIZE, MPI_CHAR, status.MPI_SOURCE, 8, MPI_COMM_WORLD);
            }

            // add peer to list and send it
            get<0>(files[string(filename)]).push_back(status.MPI_SOURCE);
            vector<int> peers = get<0>(files[string(filename)]);
            int num_peers = peers.size();
            MPI_Send(&num_peers, 1, MPI_INT, status.MPI_SOURCE, 9, MPI_COMM_WORLD);
            MPI_Send(peers.data(), num_peers, MPI_INT, status.MPI_SOURCE, 10, MPI_COMM_WORLD);

        // swarm update request
        } else if (request_tag == SWARN_UPDATE) {
            // recv filename
            char filename[MAX_FILENAME + 1];
            memset(filename, 0, MAX_FILENAME + 1);
            MPI_Recv(filename, MAX_FILENAME, MPI_CHAR, status.MPI_SOURCE, 11, MPI_COMM_WORLD, &status);

            // send swarm
            vector<int> &peers = get<0>(files[string(filename)]);
            int num_peers = peers.size();
            MPI_Send(&num_peers, 1, MPI_INT, status.MPI_SOURCE, 12, MPI_COMM_WORLD);
            MPI_Send(peers.data(), num_peers, MPI_INT, status.MPI_SOURCE, 13, MPI_COMM_WORLD);
        
        // finished signal
        } else if (request_tag == FINISHED_DOWNLOADING) {
            peers_done[status.MPI_SOURCE] = true;
        }
        
        // if downloads are done send finish signal
        if (all_done(peers_done)) {
            for (int i = 1; i < numtasks; i++) {
                int done = 0;
                MPI_Send(&done, 1, MPI_INT, i, 17, MPI_COMM_WORLD);
            }
            break;
        }
    }
}

// function for reading the input file
in_data *read_input(int rank) {
    string filename = "in" + to_string(rank) + ".txt";
    ifstream fin(filename);

    if (!fin.is_open()) {
        printf("Rank %d: Error on opening input file\n", rank);
        exit(-1);
    }

    in_data *data = new in_data();

    data->rank = rank;

    fin >> data->num_files_had;

    for (int i = 0; i < data->num_files_had; i++) {
        string filename;
        int num_chunks;
        vector<string> chunks;

        fin >> filename >> num_chunks;

        if (data->hashes.find(filename) == data->hashes.end())
            data->hashes[filename] = vector<string>();

        for (int j = 0; j < num_chunks; j++) {
            string chunk;
            fin >> chunk;
            chunks.push_back(chunk);
            data->hashes[filename].push_back(chunk);
        }

        data->files_had[filename] = make_pair(num_chunks, chunks);

        vector<bool> downloaded_chunks(num_chunks, true);
        data->downloaded_chunks[filename] = downloaded_chunks;
    }

    fin >> data->num_files_wanted;
    for (int i = 0; i < data->num_files_wanted; i++) {
        string filename;
        fin >> filename;
        data->files_wanted.push_back(filename);
    }

    fin.close();
    return data;
}

// peer function
void peer(int numtasks, int rank) {
    // read input
    in_data *data = read_input(rank);

    pthread_mutex_init(&data->mutex, NULL);

    // send data to tracker
    MPI_Send(&data->num_files_had, 1, MPI_INT, TRACKER_RANK, 0, MPI_COMM_WORLD);
    for (auto it = data->files_had.begin(); it != data->files_had.end(); it++) {
        string filename = it->first;
        int num_chunks = it->second.first;
        vector<string> chunks = it->second.second;

        MPI_Send(filename.c_str(), MAX_FILENAME, MPI_CHAR, TRACKER_RANK, 1, MPI_COMM_WORLD);
        MPI_Send(&num_chunks, 1, MPI_INT, TRACKER_RANK, 2, MPI_COMM_WORLD);
        
        for (int i = 0; i < num_chunks; i++) {
            MPI_Send(chunks[i].c_str(), HASH_SIZE, MPI_CHAR, TRACKER_RANK, 3, MPI_COMM_WORLD);
        }
    }

    // wait for start signal
    int start_signal;
    MPI_Recv(&start_signal, 1, MPI_INT, TRACKER_RANK, 4, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    // start downloading and uploading threads
    pthread_t download_thread;
    pthread_t upload_thread;
    void *status;
    int r;

    r = pthread_create(&download_thread, NULL, download_thread_func, (void *)data);
    if (r) {
        printf("Eroare la crearea thread-ului de download\n");
        exit(-1);
    }

    r = pthread_create(&upload_thread, NULL, upload_thread_func, (void *)data);
    if (r) {
        printf("Eroare la crearea thread-ului de upload\n");
        exit(-1);
    }

    r = pthread_join(download_thread, &status);
    if (r) {
        printf("Eroare la asteptarea thread-ului de download\n");
        exit(-1);
    }

    r = pthread_join(upload_thread, &status);
    if (r) {
        printf("Eroare la asteptarea thread-ului de upload\n");
        exit(-1);
    }

    pthread_mutex_destroy(&data->mutex);
}
 
int main (int argc, char *argv[]) {
    int numtasks, rank;
 
    int provided;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
    if (provided < MPI_THREAD_MULTIPLE) {
        fprintf(stderr, "MPI nu are suport pentru multi-threading\n");
        exit(-1);
    }
    MPI_Comm_size(MPI_COMM_WORLD, &numtasks);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    if (rank == TRACKER_RANK) {
        tracker(numtasks, rank);
    } else {
        peer(numtasks, rank);
    }

    MPI_Finalize();
}
