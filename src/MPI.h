#pragma once 

// Install MPI via
// sudo apt install openmpi-bin openmpi-common openmpi-doc libopenmpi-dev

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

template<typename T> 
void mpi_return(T& x) {
	assert(mpi_rank() != MPI_HEAD_RANK && "*** Head rank cannot call mpi_return"); // the head can't return 
	
	std::string v = x.serialize(); // convert to string 
//	CERR "MPI SENDING " TAB mpi_rank() TAB v ENDL;
	
	MPI_Send(v.data(), v.size(), MPI_CHAR, MPI_HEAD_RANK, 0, MPI_COMM_WORLD);
}

template<typename T>
std::vector<T> mpi_gather() {
	// read all of the MPI returns (of type T)
	// Note that the output does not come with any order guarantees
	// and waits until all are finished 
	//https://mpitutorial.com/tutorials/dynamic-receiving-with-mpi-probe-and-mpi-status/

	// NOTE: This is super non-robust to losing nodes or getting them quickly back
	// in order
	
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


