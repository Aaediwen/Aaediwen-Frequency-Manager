Welcome to my new frequency manager module for SDR++. I wrote this initially to add color coded frequency lists to the frequency manager included with SDR++

While working on this project, I also noticed more and more things I didn't like about the frequency manager that came with SDR++, so I ended up writing my own.

This frequency manager includes:
	
	Color coded frequency lists
	reduced file I/O versus the default
	frequency import and export compatable with the default manager

To build, add the aaed frequency manager under the misc modules in the SDR++ source tree, and edit the SDR++ CMakeLists.txt to include the aaed_frequency_manager directory
Then build SDR++ as normal.

I have tested this under Windows and Linux. I do not own a Mac to test on.

One notable feature I am considering adding is support for sharing a list of frequencies between multiple instances, possibly hosted via a SQL engine, and then also support falling back to a local config if it can't talk to the database.
