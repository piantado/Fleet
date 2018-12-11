#pragma once

void parallelize( void *f(void*), size_t cores ) {
	
	pthread_t threads[cores]; 
		
	for(unsigned long t=0;t<cores;t++) {
		int rc = pthread_create(&threads[t], nullptr, f, nullptr);
		if(rc) assert(0);
	}
		
	// wait for all to complete
	for(unsigned long t=0;t<cores;t++) {
		pthread_join(threads[t], nullptr);     
	}
}
