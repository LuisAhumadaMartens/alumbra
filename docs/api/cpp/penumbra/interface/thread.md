\file penumbra/interface/thread.h
Hook through which a port creates the worker threads of Umbra's thread pool.

The hook uses only the standard library, so a port implements it without Umbra or Abseil headers. It lets the
port name threads, set their priorities or attach them to a runtime such as the JVM.

\class penumbra::ThreadHandle
Joinable handle to a thread started by a ThreadFactory.

\fn virtual void penumbra::ThreadHandle::Join()=0
Blocks until the thread's body has finished, then releases the thread.

Call it once, before destroying the handle, on a teardown path: never on the joined thread, the control sequence
or an audio thread. It has no failure result.

\class penumbra::ThreadFactory
Creates the threads of Umbra's thread pool.

Implementations must not throw. The factory must outlive every pool that uses it.

\fn virtual std::unique_ptr<ThreadHandle> penumbra::ThreadFactory::StartThread(std::string_view name, std::function<void()> body)=0
Starts a thread named `name` that runs `body`, and returns its handle, or null when the thread cannot start.

It runs on the worker-creation path, never inside a player control or audio callback. `name` is borrowed for the
call, so an implementation copies it; `body` belongs to the implementation once the call returns. When it
returns null, `ThreadPool::Create` reports an error and joins the threads it has already started.
