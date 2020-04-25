# fancy quiz server

My take on a POSIX-compliant server that manages multiple user-created quiz games (like Kahoot!) in a CLI

 - written in **C** using **POSIX multithreading, multiplexing** and **sockets**
 - a user can be an admin or a player
 - admins can create quizzes for others to answer
 - each quiz is run in a thread, while the players are managed by multiplexing (polling)
