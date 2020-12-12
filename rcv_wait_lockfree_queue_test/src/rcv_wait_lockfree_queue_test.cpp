//============================================================================
// Name        : rcv_wait_lockfree_queue_test.cpp
// Author      :
// Version     :
// Copyright   : Your copyright notice
// Description : Hello World in C++, Ansi-style
//============================================================================

#include <iostream>
#include <pthread.h>
#include <stdint.h>

#include "rcv_wait_lockfree_queue.hpp"

pthread_barrier_t barrier;

struct thread_arg {
	rcv_wait_lockfree_queue<uintptr_t>* p_queue1 = nullptr;
	rcv_wait_lockfree_queue<uintptr_t>* p_queue2 = nullptr;

	static int loop_num;

	thread_arg( void )
	{
		p_queue1 = new rcv_wait_lockfree_queue<uintptr_t>;
		p_queue2 = new rcv_wait_lockfree_queue<uintptr_t>;
	}

	~thread_arg()
	{
		delete p_queue2;
		delete p_queue1;
	}
};

int thread_arg::loop_num = 1000;

/**
 * 各スレッドのメインルーチン。
 * queueへのenqueue, dequeueを繰り返す。
 */
void* func_sender( void* data )
{
	hazard::hazard_context hazard_context;

	thread_arg* p_arg      = reinterpret_cast<thread_arg*>( data );
	int         loop_count = thread_arg::loop_num;

	pthread_barrier_wait( &barrier );

	// queueへのenqueue, dequeueをloop_count回繰り返す。
	// 繰り返しごとに、dequeueした値に1加えたものをenqueueしていく。
	uintptr_t element = 0;
	while ( loop_count > 0 ) {
		loop_count--;
		element++;
		p_arg->p_queue1->enqueue( element );

		p_arg->p_queue2->dequeue_wait( &element );

		//		std::cout << "received: " << std::to_string( element ) << std::endl;
	}
	p_arg->p_queue1->enqueue( 0 );

	return reinterpret_cast<void*>( element );
}

void* func_receiver( void* data )
{
	hazard::hazard_context hazard_context;

	thread_arg* p_arg = reinterpret_cast<thread_arg*>( data );

	pthread_barrier_wait( &barrier );

	uintptr_t element = 0;
	while ( true ) {
		p_arg->p_queue1->dequeue_wait( &element );
		if ( element == 0 ) break;
		p_arg->p_queue2->enqueue( element );
	};

	return reinterpret_cast<void*>( element );
}

int test1( int num_thread )
{
	std::cout << "TEST1" << std::endl;   // prints !!!Hello World!!!

	pthread_barrier_init( &barrier, NULL, num_thread * 2 );
	pthread_t* threads = new pthread_t[num_thread * 2];
	thread_arg thread_arg_one;

	for ( int i = 0; i < num_thread; i++ ) {
		pthread_create( &threads[i * 2], NULL, func_sender, reinterpret_cast<void*>( &thread_arg_one ) );
		pthread_create( &threads[i * 2 + 1], NULL, func_receiver, reinterpret_cast<void*>( &thread_arg_one ) );
	}

	int sum = 0;
	for ( int i = 0; i < num_thread * 2; i++ ) {
		uintptr_t e;
		pthread_join( threads[i], reinterpret_cast<void**>( &e ) );
		std::cout << "Thread " << i << ": last dequeued = " << e << std::endl;
		sum += e;
	}
	// 各スレッドが最後にdequeueした値の合計は num_thread * num_loop に等しくなるはず。
	std::cout << "Sum: " << sum << std::endl;
	if ( sum == num_thread * thread_arg::loop_num ) {
		std::cout << "OK!" << std::endl;
	} else {
		std::cout << "NG!!!  expect: " << std::to_string( num_thread * thread_arg::loop_num ) << std::endl;
	}

	delete[] threads;

	return 0;
}

int test2( int num_thread )
{
	std::cout << "TEST2" << std::endl;   // prints !!!Hello World!!!

	pthread_barrier_init( &barrier, NULL, num_thread * 2 );
	pthread_t*  threads     = new pthread_t[num_thread * 2];
	thread_arg* thread_args = new thread_arg[num_thread];

	for ( int i = 0; i < num_thread; i++ ) {
		pthread_create( &threads[i * 2], NULL, func_sender, reinterpret_cast<void*>( &thread_args[i] ) );
		pthread_create( &threads[i * 2 + 1], NULL, func_receiver, reinterpret_cast<void*>( &thread_args[i] ) );
	}

	int sum = 0;
	for ( int i = 0; i < num_thread * 2; i++ ) {
		uintptr_t e;
		pthread_join( threads[i], reinterpret_cast<void**>( &e ) );
		std::cout << "Thread " << i << ": last dequeued = " << e << std::endl;
		sum += e;
	}
	// 各スレッドが最後にdequeueした値の合計は num_thread * num_loop に等しくなるはず。
	std::cout << "Sum: " << sum << std::endl;
	if ( sum == num_thread * thread_arg::loop_num ) {
		std::cout << "OK!" << std::endl;
	} else {
		std::cout << "NG!!!  expect: " << std::to_string( num_thread * thread_arg::loop_num ) << std::endl;
	}

	delete[] threads;

	return 0;
}
int main( int argc, char* argv[] )
{
	std::cout << "!!!Hello World!!!" << std::endl;   // prints !!!Hello World!!!

	int num_thread       = 16;
	thread_arg::loop_num = 1000000;

	test1( num_thread );
	test2( num_thread );

	return 0;
}
