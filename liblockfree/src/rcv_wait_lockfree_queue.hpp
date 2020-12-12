/*
 * rcv_wait_lockfree_queue.hpp
 *
 *  Created on: 2020/12/12
 *      Author: alpha
 */

#ifndef SRC_RCV_WAIT_LOCKFREE_QUEUE_HPP_
#define SRC_RCV_WAIT_LOCKFREE_QUEUE_HPP_

#include <semaphore.h>

#include "queue_hazard.hpp"

template <typename T>
class rcv_wait_lockfree_queue : private lockfree_hazard::Queue<T> {
public:
	rcv_wait_lockfree_queue( void )
	  : lockfree_hazard::Queue<T>()
	{
		p_sem = new sem_t;
		if ( sem_init( p_sem, 0, 0 ) == -1 ) {
			printf( "sem_init" );
			delete p_sem;
			p_sem = nullptr;
		}
	}

	rcv_wait_lockfree_queue( const rcv_wait_lockfree_queue& ) = delete;
	rcv_wait_lockfree_queue& operator=( const rcv_wait_lockfree_queue& ) = delete;
	rcv_wait_lockfree_queue( rcv_wait_lockfree_queue& )                  = delete;
	rcv_wait_lockfree_queue& operator=( rcv_wait_lockfree_queue& ) = delete;

	~rcv_wait_lockfree_queue()
	{
		if ( p_sem != nullptr ) {
			sem_destroy( p_sem );
			delete p_sem;
		}
	}

	/**
	 * キューへ、値をアトミックに投入する。
	 * @param args 投入する値のコンストラクタ引数になる任意のパラメータ。
	 */
	template <typename... Args>
	void enqueue( Args&&... args )
	{
		lockfree_hazard::Queue<T>::enqueue( std::forward<Args>( args )... );
		sem_post( p_sem );
	}

	/**
	 * キューから、値をアトミックに取り出す。
	 * キューが空であれば、何も行なわずfalseを返す。
	 * @param out 取り出した値を格納する変数へのポインタ。
	 * @return キューからの取り出しに成功した場合はtrue。
	 */
	bool dequeue_try( T* out )
	{
		return lockfree_hazard::Queue<T>::dequeue( [out]( T& value ) {
			*out = std::move( value );
		} );
	}

	/**
	 * キューから、値をアトミックに取り出す。
	 * キューが空であれば、キューに投入されるまで、ウェイトする。
	 * @param out 取り出した値を格納する変数へのポインタ。
	 * @return キューからの取り出しに成功した場合はtrue。
	 */
	bool dequeue_wait( T* out )
	{

		while ( true ) {
			bool deq_ret = dequeue_try( out );
			if ( deq_ret ) break;

			sem_wait( p_sem );
		}

		return true;
	}

private:
	sem_t* p_sem = nullptr;
};

#endif /* SRC_RCV_WAIT_LOCKFREE_QUEUE_HPP_ */
