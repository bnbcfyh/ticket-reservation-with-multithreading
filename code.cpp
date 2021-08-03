#include <iostream>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <pthread.h>
#include <semaphore.h>

using namespace std;

int capacity; // capacity of the saloon
vector<bool> seats; // vector showing whether the seats are full or not
string teller_codes[] = { "A", "B", "C" }; // strings to print tellers' names
fstream output_file; // output file object

// size of this vector will be equal to the number of clients. this will show whether each client's job is done or not.
vector<bool> is_served;

vector<bool> is_free; // vector showing whether the given teller is available

bool is_finished = false; // turns to true when the entire job is finished.

// data of the current client that tellers are serving. there are separate one for each teller.
vector<vector<int>*> currently_serving;

pthread_mutex_t client_mutex = PTHREAD_MUTEX_INITIALIZER; // lock used to keep clients in the queue

pthread_mutex_t teller_hello = PTHREAD_MUTEX_INITIALIZER; // lock used while tellers print "they have arrived"
pthread_mutex_t make_reservation = PTHREAD_MUTEX_INITIALIZER; // lock used while tellers reserve tickets
pthread_mutex_t print_result = PTHREAD_MUTEX_INITIALIZER; // lock used while tellers print the result

sem_t tellers_available; // semaphore to control when three of the tellers are busy

/*
 * Client thread is used in order to handle the client's duties. Waiting in the line, then
 * picking a teller, and leaving when done.
 */
void* client(void *param) {

	vector<int> *data = static_cast<vector<int>*>(param);
	int arrival_time = data->at(0);
	int client_id = data->at(3);

	usleep(arrival_time * 1000); // sleep for arrival time

	pthread_mutex_lock(&client_mutex);

	int chosen_id = -1;
	while (chosen_id < 0) {

		sem_wait(&tellers_available);

		/*
		 * Start by checking from the first so that the client will
		 * prioritize the teller with the lowest number.
		 */
		bool test0 = is_free[0];
		bool test1 = is_free[1];
		bool test2 = is_free[2];

		if (test0) {
			currently_serving[0] = data;
			chosen_id = 0;
			is_free[0] = false;
			break;
		} else if (test1) {
			currently_serving[1] = data;
			chosen_id = 1;
			is_free[1] = false;
			break;
		} else if (test2) {
			currently_serving[2] = data;
			chosen_id = 2;
			is_free[2] = false;
			break;
		}
	}
	pthread_mutex_unlock(&client_mutex);

	while (true) { // wait before going to the exit for teller to finish its job.
		if (is_served[client_id]) { // when served, free the resources and break.
			sem_post(&tellers_available);
			currently_serving[chosen_id] = NULL;
			is_free[chosen_id] = true;
			break;
		}
	}

	pthread_exit(NULL);
}

/*
 * This thread handles all duties of the tellers. Making reservations, printing the results, etc.
 */
void* teller(void *param) {

	int id = *(int*) param;

	// lock while printing they have arrived
	pthread_mutex_lock(&teller_hello);
	output_file << "Teller " + teller_codes[id] + " has arrived." << endl;
	pthread_mutex_unlock(&teller_hello);

	while (!is_finished) {
		if (!is_free[id]) { // is_free is false only if this teller is signaled by a client

			vector<int> *data = currently_serving[id];

			int service_time = data->at(1);
			int requested_seat_index = data->at(2) - 1;
			int client_id = data->at(3);

			// lock while making the reservation so that the seat won't be available for the others.
			pthread_mutex_lock(&make_reservation);

			string result;
			if (!seats[requested_seat_index]) { // if the seat is empty
				seats[requested_seat_index] = true;
				result = "Client" + to_string(client_id) + " requests seat "
						+ to_string(requested_seat_index + 1)
						+ ", reserves seat "
						+ to_string(requested_seat_index + 1)
						+ ". Signed by Teller " + teller_codes[id] + ".";
			}

			else { // if the seat is full

				int min_index = -1;
				for (int i = 0; i < capacity; ++i) { // pick a seat with the lowest number
					if (!seats[i]) {
						min_index = i;
						break;
					}
				}

				if (min_index < 0) { // no available seat, could not find any from the for loop above

					result = "Client" + to_string(client_id) + " requests seat "
							+ to_string(requested_seat_index + 1)
							+ ", reserves None " + ". Signed by Teller "
							+ teller_codes[id] + ".";

				} else {
					seats[min_index] = true;
					result = "Client" + to_string(client_id) + " requests seat "
							+ to_string(requested_seat_index + 1)
							+ ", reserves seat " + to_string(min_index + 1)
							+ ". Signed by Teller " + teller_codes[id] + ".";
				}

			}

			pthread_mutex_unlock(&make_reservation);

			usleep(service_time * 1000);

			// print the result
			pthread_mutex_lock(&print_result);
			output_file << result << endl;
			is_served[client_id] = true;
			pthread_mutex_unlock(&print_result);

		}
	}
	pthread_exit(NULL);
}

// a utility method to split string according to a given token
vector<string> split(string str, string token) {
	vector<string> result;
	while (true) {
		int index = str.find(token);
		if (index != string::npos) {
			result.push_back(str.substr(0, index));
			str = str.substr(index + token.size());
			if (str.size() == 0)
				result.push_back(str);
		} else {
			result.push_back(str);
			break;
		}
	}
	return result;
}

int main(int argc, char *argv[]) {

	/*
	 * initialize the data in the global field.
	 */
	is_free.push_back(true);
	is_free.push_back(true);
	is_free.push_back(true);

	currently_serving.push_back(NULL);
	currently_serving.push_back(NULL);
	currently_serving.push_back(NULL);

	string input_path = argv[1];
	ifstream input_file(input_path.c_str());

	string output_path = argv[2];
	output_file.open(output_path, ios::out);

	// initialize the semaphore
	sem_init(&tellers_available, 0, 3);

	output_file << "Welcome to the Sync-Ticket!" << endl;

	int number_of_clients;
	vector<vector<int>> clients_data;

	// read the input and hold the values in the variables.
	if (!input_file.is_open()) // file is not open
		return -1;

	int count = 0;
	string str;
	while (getline(input_file, str)) {
		if (count == 0) { // first line
			if (str == "OdaTiyatrosu") {
				capacity = 60;
			} else if (str == "UskudarTiyatroSahne") {
				capacity = 80;
			} else {
				capacity = 200;
			}

			for (int i = 0; i < capacity; ++i) {
				seats.push_back(false);
			}

			count++;
		} else if (count == 1) { // second line
			number_of_clients = stoi(str);
			for (int i = 0; i < number_of_clients; ++i) {
				is_served.push_back(false);
			}

			count++;
		} else { // n lines of input

			vector<string> line = split(str, ",");

			vector<int> current_data;

			for (int i = 1; i < line.size(); ++i) {
				int number = stoi(line[i]);
				current_data.push_back(number);

			}

			clients_data.push_back(current_data);
		}
	}

	for (int i = 0; i < clients_data.size(); ++i) // keep the client id's at the last index.
		clients_data[i].push_back(i + 1);

	/*
	 * Create Teller threads.
	 */
	vector<pthread_t> teller_tid_list;
	int offset0 = 0;
	int offset1 = 1;
	int offset2 = 2;

	pthread_t thread_id1;
	pthread_create(&thread_id1, NULL, teller, &offset0);
	teller_tid_list.push_back(thread_id1);

	pthread_t thread_id2;
	pthread_create(&thread_id2, NULL, teller, &offset1);
	teller_tid_list.push_back(thread_id2);

	pthread_t thread_id3;
	pthread_create(&thread_id3, NULL, teller, &offset2);
	teller_tid_list.push_back(thread_id3);

	/*
	 * Create Client threads.
	 */
	vector<pthread_t> client_tid_list;
	for (int i = 0; i < clients_data.size(); ++i) {
		pthread_t thread_id;
		pthread_create(&thread_id, NULL, client, (void*) &clients_data[i]);
		client_tid_list.push_back(thread_id);
	}

	for (int i = 0; i < client_tid_list.size(); ++i) { // wait for all customers to get served.
		pthread_join(client_tid_list[i], NULL);
	}

	is_finished = true;

	output_file << "All clients receieved service." << endl;

	return 0;
}
