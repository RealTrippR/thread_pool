
/* a simple fix for the recursive deadlock problem: if all threads are executing a function
    and launchTask is called, just run that task (instead of pushing it to the task queue) 
    when it's called */


//void 