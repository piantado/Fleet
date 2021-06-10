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
	
	MPI_Send(v.data(), v.size(), MPI_CHAR, MPI_HEAD_RANK, 0, MPI_COMM_WORLD);
}

template<typename T>
std::vector<T> mpi_gather() {
	// read all of the MPI returns (of type T)
	// Note that the output does not come with any order guarantees
	// and waits until all are finished 
	//https://mpitutorial.com/tutorials/dynamic-receiving-with-mpi-probe-and-mpi-status/

	assert(mpi_rank() == MPI_HEAD_RANK && "*** Cannot call mpi_gather unless you are head rank"); 
	
	int s = mpi_size();
	std::vector<T> out;	
	
	for(int r=0;r<s;r++) {
		if(r != MPI_HEAD_RANK) {
			
			// get the status with the message size
			MPI_Status status;
			MPI_Probe(r, 0, MPI_COMM_WORLD, &status);

			// When probe returns, the status object has the size and other
			// attributes of the incoming message. Get the message size
			int sz; MPI_Get_count(&status, MPI_INT, &sz);

			// Allocate a buffer to hold the incoming numbers
			char buf[sz];

			// Now receive the message with the allocated buffer
			MPI_Recv(buf, sz, MPI_INT, r, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

			out.push_back(T::deserialize(std::string(buf)));
		}
	}
	
	return out;	
}


