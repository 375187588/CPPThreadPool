#include "ThreadPool.h"
#include <iostream>

namespace TPool 
{
	const int TASK_MAX_THRESHHOLD = INT32_MAX;
	const int THREAD_MAX_THRESHHOLD = 1024;
	const int THREAD_MAX_IDLE_TIME = 10;// 60;//��λ����

	//�̳߳ع���
	ThreadPool::ThreadPool() :
		initThreadSize_(0)
		, taskSize_(0)
		, idleThreadSize_(0)
		, curThreadSize_(0)
		, taskQueMaxThreshHold_(TASK_MAX_THRESHHOLD)
		, threadSizeThreshHold_(THREAD_MAX_THRESHHOLD)
		, poolMode_(PoolMode::MODE_FIXED)
		, isPoolRunning_(false)

	{
	}

	//�̳߳�����
	ThreadPool::~ThreadPool()
	{
		isPoolRunning_ = false;
		//�ȴ��̳߳��������е��̷߳��� ������״̬������������ִ��������
		std::unique_lock<std::mutex> lock(taskQueMtx_);

		notEmpty_.notify_all();
		exitCond_.wait(lock, [&]()->bool {return threads_.size() == 0; });
	}

	//�����̳߳صĹ���ģʽ
	void ThreadPool::setMode(PoolMode mode)
	{
		if (checkRunningState())
			return;
		poolMode_ = mode;
	}

	//�����̳߳�cachedģʽ���߳���ֵ
	void ThreadPool::setThreadSizeThreshHold(int threshhold)
	{
		if (checkRunningState())
			return;

		if (poolMode_ == PoolMode::MODE_CACHED)
		{
			threadSizeThreshHold_ = threshhold;
		}
	}

	//����task�������������ֵ
	void ThreadPool::setTaskQueMaxThreashHold(int threshhold)
	{
		if (checkRunningState())
			return;

		taskQueMaxThreshHold_ = threshhold;
	}

	//���̳߳��ύ���� �û����øýӿڣ��������������������
	Result ThreadPool::submitTask(std::shared_ptr<Task> sp)
	{
		//��ȡ��
		std::unique_lock<std::mutex> lock(taskQueMtx_);
		//�̵߳�ͨ�� �ȴ�������� �п��� wait wait_for wait_until
		//�û��ύ���� �������������1s�����ж��ύ����ʧ��,����

		if (!notFull_.wait_for(lock, std::chrono::seconds(1), [&]()->bool {return taskQue_.size() < (size_t)taskQueMaxThreshHold_; }))
		{
			//��ʾnotFull_�ȴ�1s�ӣ�������Ȼ����
			std::cerr << "task queue is full,submit task fail." << std::endl;
			return Result(sp, false);
		}

		//����п��࣬������������������
		taskQue_.emplace(sp);
		taskSize_++;

		//��Ϊ�·�������������п϶������ˣ���notEmpty_�Ͻ���֪ͨ���Ͽ�����߳�ִ������
		notEmpty_.notify_all();

		//fixedģʽ �ȽϺ�ʱ

		//cachedģʽ ������ȽϽ�����������С���������
		//��Ҫ�������������Ϳ����̵߳��������ж��Ƿ���Ҫ�����µ��̳߳���
		if (poolMode_ == PoolMode::MODE_CACHED &&
			taskSize_ > idleThreadSize_ &&
			curThreadSize_ < threadSizeThreshHold_)
		{
			//�������߳�
			std::cout << " >>> create new thread.." << std::endl;
			auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
			int threadId = ptr->getId();
			threads_.emplace(threadId, std::move(ptr));
			threads_[threadId]->start();//�����߳�
			curThreadSize_++;
			idleThreadSize_++;
		}

		//���������Result����
		return Result(sp);
	}


	//�����̳߳�
	void ThreadPool::start(int initThreadSize)
	{
		//�����̳߳صĶ���״̬
		isPoolRunning_ = true;
		//��¼��ʼ�̸߳���
		initThreadSize_ = initThreadSize;
		curThreadSize_ = initThreadSize;

		//�����̶߳���
		for (int i = 0; i < initThreadSize; ++i)
		{
			//����thread�̶߳����ʱ�򣬰��̺߳�������thread�̶߳���
			auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
			int threadId = ptr->getId();
			threads_.emplace(threadId, std::move(ptr));
		}
		//���������߳�
		for (int i = 0; i < initThreadSize_; ++i)
		{
			//��Ҫȥִ��һ���̺߳���
			threads_[i]->start();
			//��¼��ʼ�����̵߳�����
			idleThreadSize_++;
		}
	}

	void ThreadPool::threadFunc(int threadid)
	{
		auto lastTime = std::chrono::high_resolution_clock().now();
		while (isPoolRunning_)
		{
			std::shared_ptr<Task> task;
			{
				//�Ȼ�ȡ��
				std::unique_lock<std::mutex> lock(taskQueMtx_);
				std::cout << "tid:" << std::this_thread::get_id() << "���Ի�ȡ����..." << std::endl;
				//cachedģʽ�£��п����Ѿ������˺ܶ���̣߳����ǿ���ʱ�䳬��60s,Ӧ�ðѶ�����߳�
				//�������յ�(����initThreadSize_�������߳�Ҫ���л���)
				//��ǰʱ�� - ��һ���߳�ִ�е�ʱ�� > 60s

				//ÿһ���ӷ���һ�� ��ô���֣���ʱ����?�����������ִ�з���
				//��+˫���ж�
				while (isPoolRunning_ && taskQue_.size() == 0)
				{
					if (poolMode_ == PoolMode::MODE_CACHED)
					{
						//������������ʱ����
						if (std::cv_status::timeout == notEmpty_.wait_for(lock, std::chrono::seconds(1)))
						{
							auto now = std::chrono::high_resolution_clock().now();
							auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
							if (dur.count() >= THREAD_MAX_IDLE_TIME &&
								curThreadSize_ > initThreadSize_)
							{
								//��ʼ���յ�ǰ�߳�
								//��¼�߳���������ر�����ֵ���޸�
								//���̶߳�����߳��б�������ɾ�� û�а취
								//threadid->thread����->ɾ��
								threads_.erase(threadid);
								curThreadSize_--;
								idleThreadSize_--;
								std::cout << "threadid:" << std::this_thread::get_id() << std::endl;
								return;
							}
						}
					}
					else
					{
						//�ȴ�notEmpty����
						notEmpty_.wait(lock);
					}
				}
				//�̳߳�Ҫ�����������߳���Դ
				if (!isPoolRunning_)
				{
					break;
				}

				idleThreadSize_--;
				std::cout << "tid:" << std::this_thread::get_id() << "��ȡ����ɹ�..." << std::endl;

				//�����������ȡ��һ���������
				task = taskQue_.front();
				taskQue_.pop();
				taskSize_--;
				//�����Ȼ��ʣ�����񣬼���֪ͨ�������߳�ִ������
				if (taskQue_.size() > 0)
				{
					notEmpty_.notify_all();
				}

				//ȡ��һ�����񣬽���֪ͨ��֪ͨ���Լ����ύ��������
				notFull_.notify_all();
			}//��Ӧ�ð����ͷŵ�

			//��ǰ�̸߳���ִ���������
			if (task != nullptr)
			{
				//task->run();//ִ������;������ķ���ֵsetVal��������Result
				task->exec();
			}

			idleThreadSize_++;
			//�����߳�ִ���������ʱ��
			lastTime = std::chrono::high_resolution_clock().now();
		}
		threads_.erase(threadid);
		std::cout << "threadid:" << std::this_thread::get_id() << " exit!" << std::endl;
		exitCond_.notify_all();
	}

	bool ThreadPool::checkRunningState() const
	{
		return isPoolRunning_;
	}

	int Thread::generateId_ = 0;

	//�̷߳���ʵ��
	Thread::Thread(ThreadFunc func)
		:func_(func)
		, threadId_(generateId_++)
	{

	}


	Thread::~Thread()
	{
	}

	//�����߳�
	void Thread::start()
	{
		std::thread t(func_, threadId_);
		t.detach();
	}

	int Thread::getId() const
	{
		return threadId_;
	}

	Task::Task() :result_(nullptr)
	{

	}

	void Task::exec()
	{
		if (result_ != nullptr)
		{
			result_->setVal(run());
		}
	}

	void Task::setResult(Result* res)
	{
		result_ = res;
	}

	Result::Result(std::shared_ptr<Task> task, bool isValid) :
		isValid_(isValid)
		, task_(task)
	{
		task_->setResult(this);
	}

	void Result::setVal(Any any)
	{
		//�洢task�ķ���ֵ
		this->any_ = std::move(any);
		sem_.post();//�Ѿ��������ķ���ֵ�����ź�����Դ
	}

	Any Result::get()
	{
		if (!isValid_)
		{
			return "";
		}

		sem_.wait();//task�������û��ִ���꣬����������û����߳�
		return std::move(any_);
	}
};