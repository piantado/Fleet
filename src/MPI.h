#pragma once 

// Install MPI via
// sudo apt install openmpi-bin openmpi-common openmpi-doc libopenmpi-dev
// Maybe see: https://www.slothparadise.com/use-mpi-without-nfs/


#include <string>
#include <mpi.h>

const size_t MPI_HEAD_RANK = 0; // rank of the head node

int mpi_rank() {
	int r;
    MPI_Comm_rank(MPI_COMM_WORLD, &r);
	return r;
}

int mpi_size() {
	int s;
	MPI_Comm_size(MPI_COMM_WORLD, &s);
	return s;
}

bool is_mpi_head() {
	return mpi_rank() == MPI_HEAD_RANK;
}

/**
 * @brief Return my results via MPI
 * @param x
 */

template<typename T> 
void mpi_return(T& x) {
	assert(mpi_rank() != MPI_HEAD_RANK && "*** Head rank cannot call mpi_return"); // the head can't return 
	
	std::string v = x.serialize(); // convert to string 
//	CERR "MPI SENDING " TAB mpi_rank() TAB v ENDL;
	
	MPI_Send(v.data(), v.size(), MPI_CHAR, MPI_HEAD_RANK, 0, MPI_COMM_WORLD);
}

/**
 * @brief Reads all the MPI returns from mpi_return. NOTE that the output does not come with any order 
 *        guarantees. This waits until all are finished. 
 * 		  NOTE: This has NO robustness to losing nodes. 
 * 			https://mpitutorial.com/tutorials/dynamic-receiving-with-mpi-probe-and-mpi-status/
 * @return 
 */
template<typename T>
std::vector<T> mpi_gather() {

	assert(mpi_rank() == MPI_HEAD_RANK && "*** Cannot call mpi_gather unless you are head rank"); 
	
	int s = mpi_size();
	std::vector<T> out;	
	
	for(int r=0;r<s;r++) {
		if(r != MPI_HEAD_RANK) {
			// get the status with the message size
			MPI_Status status;
			auto v = MPI_Probe(r, 0, MPI_COMM_WORLD, &status);
			assert(v == MPI_SUCCESS);
			
			// When probe returns, the status object has the size and other
			// attributes of the incoming message. Get the message size
			int sz; v = MPI_Get_count(&status, MPI_CHAR, &sz);
			assert(v == MPI_SUCCESS);
			assert(sz != MPI_UNDEFINED);
			assert(sz >= 0);

			// Allocate a buffer to hold the incoming numbers
			char* buf = new char[sz+1];

			// Now receive the message with the allocated buffer
			v = MPI_Recv(buf, sz, MPI_CHAR, r, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
			assert(v == MPI_SUCCESS);
			buf[sz] = '\0'; // make null terminated
			
			std::string ret = buf;
			out.push_back(T::deserialize(ret)); // note must give size -- not null terminated
			delete[] buf; 
			
//			CERR "MPI RECEIVING" TAB r TAB sz TAB out.rbegin()->size() TAB std::string(buf, sz) ENDL;
		}
	}
	
	return out;	
}



/**
 * @brief Send an int n to each request. Each requestor can use this to index into some data
 *        which they are assumed to know already
 * @return 
 */
template<typename T>
std::vector<T> mpi_map(const size_t n) {
	enum WORKER_TAGS { GIVE_ME, TAKE_THIS, DONE };
		
	if(mpi_rank() == MPI_HEAD_RANK) {
	
		// store all of the return values
		std::vector<T> ret;
		
		for(size_t i=0;i<n;) {

			MPI_Status status;
			auto v = MPI_Probe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
			
			if(status.MPI_TAG == TAKE_THIS) { // if they are sending data
			
				// Allocate a buffer to hold the incoming numbers
				char* buf = new char[sz+1];

				// Now receive the message with the allocated buffer
				v = MPI_Recv(buf, sz, MPI_CHAR, r, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
				assert(v == MPI_SUCCESS);
				buf[sz] = '\0'; // make null terminated
				
				std::string ret = buf;
				out.push_back(T::deserialize(ret)); // note must give size -- not null terminated
				delete[] buf; 		
			}
			else if(status.MPI_TAG == GIVE_ME) {
				int[1] buf;
				buf[0] = i;
				MPI_send(buf, 1, MPI_INT, status.MPI_SOURCE, TAKE_THIS, MPI_COMM_WORLD);
				i++;
			}
			else {
				assert(false && "*** Bad tag");
			}		
		}
	}
	else {
		// It's a worker
		while(true) {
			
			MPI_Status status;
			auto v = MPI_Probe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
			
		}
	}
}

