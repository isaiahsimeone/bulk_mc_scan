#include <iostream>
#include <vector>
#include <fstream>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <list>
#include <thread>
#include <mutex>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdint.h>

#define MINECRAFT_PORT 25565
#define PAYLOAD_SZ  100
#define MAX_RESPONSE_SZ 100000

using namespace std;

void            e_exit(const char*);
void            scan_master(int, int, ifstream*, ofstream*);
void            scan_slave(list<string>, int, int, int*, int*, ofstream*);
int             encode_unsigned_varint(uint8_t* const, uint8_t);
string          minecraft_handshake(int, string);


extern char* optarg;
extern int optind, opterr, optopt;

mutex output_lock;

int main(int argc, char** argv) {
    int num_threads = 1000;
    int timeout = 500;

    char* in_file_name = NULL;
    char* out_file_name = NULL;
    int c;

    opterr = 0;

    while ((c = getopt(argc, argv, "qt:j:i:o:")) != -1)
        switch(c) {
            case 't': /* Timeout */
                timeout = atoi(optarg);
                break;
            case 'j': /* Number of threads */
                num_threads = atoi(optarg);
                break;
            case 'i':
                in_file_name = optarg;
                break;
            case 'o':
                out_file_name = optarg;
                break;
            default: /* FALLTHROUGH */
            case '?':
                if (optopt == 't' || optopt == 'j' || optopt == 'o' || optopt == 'i')
                    e_exit("Option requires argument");
                else if (isprint(optopt))
                    e_exit("Unknown option");
                return 1;
        }

    if (in_file_name == NULL || out_file_name == NULL)
        e_exit("Input (-i) and output (-o) files required");

    cout << num_threads << " slaves with " << timeout << "ms delay" << endl;

    /* Open input file */
    ifstream in_file;
    in_file.open(in_file_name, ios::in);
    if (!in_file.is_open())
        e_exit("Unable to open input file");

    /* Open output file */
    ofstream* out_file = new ofstream();
    out_file->open(out_file_name, ios::out);
    if (!out_file->is_open())
        e_exit("Unable to open output file");

    /* Ignore sigpipe */
    signal(SIGPIPE, SIG_IGN);

    /* Launch master */
    scan_master(num_threads, timeout, &in_file, out_file);
    printf("Done\n");

    return 0;
}

void scan_master(int num_threads, int timeout, ifstream* in_file, ofstream* out_file) {
    /* Divide addresses into blocks */
    vector <list<string>> blocks;
    blocks.resize(num_threads);

    int idx = 0;
    for (string line; getline(*in_file, line);)
        blocks[idx++ % num_threads].push_back(line);

    int *progress = new int[num_threads];
    int *found = new int[num_threads];

    std::thread threads[num_threads];

    int launch_number = 0;
    for (list <string> block: blocks)
        threads[launch_number++] = thread (scan_slave, block, timeout, launch_number, progress, found, out_file);

    // printer

    for (int i = 0; i < num_threads; i++)
        threads[i].join();

}

void scan_slave(list<string> addresses, int timeout, int launch_number, int* progress, int* found, ofstream* out_file) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        cerr << "Slave " << launch_number << " died" << endl;
        return ;
    }

    string out, json;
    while (!addresses.empty()) {
        // Attempt MC handshake with address
        json = minecraft_handshake(sock, addresses.back());

        if (json != "") {
            found[launch_number]++;
            output_lock.lock();
            cout << "Server found at: " << addresses.back() << endl;
            // Write to output file
            out.append(addresses.back());
            out.append(" - ");
            out.append(json);
            out.append("\n");
            out_file->write(out.c_str(), out.length());
            output_lock.unlock();
        }
        progress[launch_number]++;

        addresses.pop_back();
        usleep((timeout + 1000) * 1000);
    }
}

/*
 * Attempts to contact a minecraft server at the specified address, and returns
 * it's response.
 */
string minecraft_handshake(int sock, string address) {
    struct sockaddr_in sai;
    memset(&sai, 0, sizeof(struct sockaddr_in));
    sai.sin_family = AF_INET;
    sai.sin_addr.s_addr = inet_addr(address.c_str());
    sai.sin_port = htons(MINECRAFT_PORT);

    if (connect(sock, (struct sockaddr*)&sai, sizeof(sai)) == 0)
        return ""; /* Connection failure */

    uint8_t* payload = (uint8_t*)malloc(sizeof(uint8_t) * PAYLOAD_SZ);
    memset(payload, 0, sizeof(uint8_t) * PAYLOAD_SZ);

    uint8_t sz = 1;
    size_t addr_sz = 7;

    /* Protocol version */
    sz += encode_unsigned_varint(payload + sz, 0x04);
    /* Address length */
    sz += encode_unsigned_varint(payload + sz, addr_sz);
    /* Address string */
    payload[sz++] = '0';
    payload[sz++] = '.';
    payload[sz++] = '0';
    payload[sz++] = '.';
    payload[sz++] = '0';
    payload[sz++] = '.';
    payload[sz++] = '0';

    /* Port (25565) */
    payload[sz++] = 0x63;
    payload[sz++] = 0xDD;
    /* Handshake state */
    sz += encode_unsigned_varint(payload + sz, 0x01);

    /* Write payload size */
    if (write(sock, &sz, 1) == -1) {
        free(payload);
        return "";
    }
    /* Write payload */
    if (write(sock, payload, sz) == -1) {
        free(payload);
        return "";
    }
    free(payload);

    /* Write two bytes that server expects */
    uint8_t byte1 = 0x01;
    if (write(sock, &byte1, 1) == -1)
        return "";

    uint8_t byte2 = 0x00;
    if (write(sock, &byte2, 1) == -1)
        return "";

    /* Process response (with timeout) */
    char response[MAX_RESPONSE_SZ];
    memset(response, 0, MAX_RESPONSE_SZ);
    int read_sz = -1;
    int i = 0;
    usleep(500 * 1000);
    read_sz = read(sock, &response, MAX_RESPONSE_SZ);

    if (read_sz <= 0)
        return "";

    /* Is this JSON data? Maybe some other server hiding on
     * port 25565. Check the first 6 characters. */
    for (i = 0; i <= 5; i++) {
        if (response[i] == '{')
            break; /* JSON found */
        else if (i == 5) {
            return ""; /* JSON not found, skip */
        }
    }
    //cout << response + i /* Skip leader varint */ << endl;

    return string(response + i);
}

int encode_unsigned_varint(uint8_t* const buf, uint8_t value) {
    int encoded = 0;
    do {
        uint8_t next_byte = value & 0x7F;
        value >>= 7;

        if (value)
            next_byte |= 0x80;

        buf[encoded++] = next_byte;

    } while (value);

    return encoded;
}


void e_exit(const char* msg) {
    fprintf(stderr, "%s (errno=%s)\n", msg, strerror(errno));
    exit(1);
}