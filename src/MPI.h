#pragma once 

#include<string>
#include<mpi>

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

bool am_mpi_head() {
	return mpi_rank() == MPI_HEAD_RANK;
}

template<typename T> 
void mpi_return(T& x) {
	assert(mpi_rank() != MPI_HEAD_RANK); // the head can't return 
	
	std::string v = x.parseable(); // convert to string 
	
	MPI_Send(&v.data(), v.size(), MPI_CHAR, MPI_HEAD_RANK, 0, MPI_COMM_WORLD);
}

template<typename T>
std::vector<T> mpi_gather() {
	// read all of the MPI returns (of type T)
	// Note that the output does not come with any order guarantees
	// and waits until all are finished 
	
	assert(mpi_rank() == MPI_HEAD_RANK); 
	
	int s = mpi_size();
	std::vector<T> out;	
	
	for(int r=0;r<s;r++) {
		if(r != MPI_HEAD_RANK) {
			
			// get the status with the message size
			MPI_Status status;
			MPI_Probe(r, 0, MPI_COMM_WORLD, &status);

			// When probe returns, the status object has the size and other
			// attributes of the incoming message. Get the message size
			int sz; 
			MPI_Get_count(&status, MPI_INT, &sz);

			// Allocate a buffer to hold the incoming numbers
			char buf[sz];

			// Now receive the message with the allocated buffer
			MPI_Recv(buf, sz, MPI_INT, r, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

			/// TODO FIX 
			FIX THIS SO THAT WE PARSE IT THE RIGHT WAY 
			out.push_back(T(buf));
//https://mpitutorial.com/tutorials/dynamic-receiving-with-mpi-probe-and-mpi-status/

// MPI_Recv(&number, 1, MPI_INT, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
			
		}
	}
	
}


