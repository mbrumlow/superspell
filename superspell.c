#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <libgen.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <signal.h>
#include <string.h>

#define ASPELL_PATH "/usr/bin/aspell"
#define VERSION_ARG "-v"

int is_command(char *line, int len);
int is_camel(char *line, int len);
int aspell_normal(int *fd);
int aspell_multi(int *fd, int *copy_fd);
int pipeopen(char *path, char *const argv[], int *fd, int *copy_fd);
int copy_lines(int fd);

int main(int argc, char *argv[]) {

  int i; 
  char *line = NULL;
  size_t len = 0;
  ssize_t read;
  int current_fd;
  int aspell_normal_pid;
  int aspell_multi_pid;
  int aspell_copy_pid; 
  int aspell_normal_fd;
  int aspell_multi_fd;
  int aspell_copy_fd;
  int wstatus;

  // Check if called just for version. 
  for(i = 0; i < argc; i++) {
    len = strlen(VERSION_ARG);
    if(strlen(argv[i]) < len)
      len = strlen(argv[i]);
    
    if(strncmp(argv[i], VERSION_ARG, len) == 0){
      execv(ASPELL_PATH , argv); 
    }
  }
  
  aspell_normal_pid = aspell_normal(&aspell_normal_fd);
  if (aspell_normal_pid  == -1) { 
    perror("fork");
    exit(EXIT_FAILURE);
  }
  
  aspell_multi_pid = aspell_multi(&aspell_multi_fd, &aspell_copy_fd);
  if (aspell_multi_pid  == -1) { 
    perror("fork");
    exit(EXIT_FAILURE);
  }

  aspell_copy_pid = copy_lines(aspell_copy_fd); 
  if (aspell_copy_pid  == -1) { 
    perror("fork");
    exit(EXIT_FAILURE);
  }
  
  while((read = getline(&line, &len, stdin)) != -1) {

    // commands need to be setnt to both instances.
    if(is_command(line, read)) {
      write(aspell_multi_fd, line, read);
      write(aspell_normal_fd, line, read);
      continue;
    }
    
    if(is_camel(line, read) > 0) {
      current_fd = aspell_multi_fd;
    } else {
      current_fd = aspell_normal_fd;
    }
    write(current_fd, line, read);
  }

  kill(aspell_normal_pid, SIGKILL);
  kill(aspell_multi_pid, SIGKILL);
  kill(aspell_copy_pid, SIGKILL);
  waitpid(-1, &wstatus, 0);
}

int is_command(char *line, int len)
{
  if(len < 1) {
    return 0; 
  }

  switch(line[0]) {
  case '*':
  case '&':
  case '@':
  case '+':
  case '~':
  case '#':
  case '!':
  case '%':
  case '`':
    return 1;
  }

  return 0; 
}

int is_camel(char *line, int len)
{
  int i,j = 0; 
  int count = 0;

  if(len < 1) {
    return 0;
  }

  if(line[0] == '^') {
    j = 1; 
  }
  
  // If capitale found anywhere but the first char.
  for(i = j; i < len; i++) {
    if(line[i] >= 65 && line[i] <= 90) {
      count += (i-j);
    }
  }

  return count; 
}

int aspell_normal(int *fd)
{
  char *const argv[] = { basename(ASPELL_PATH) , "-a", "-m", "-B",
    "--encoding=utf-8", "--sug-mode=bad-spellers",  NULL };
  return pipeopen(ASPELL_PATH, argv, fd, NULL);
}

int aspell_multi(int *fd, int *copy_fd)
{
  char *const argv[] = { basename(ASPELL_PATH) , "-a", "-m", "-C",
    "--encoding=utf-8", "--sug-mode=bad-spellers",
    "--run-together-limit=5", "--run-together-min=2", NULL };
  return pipeopen(ASPELL_PATH, argv, fd, copy_fd);
}

int copy_lines(int fd)
{
  int pid; 
  FILE *fp;
  char *line = NULL;
  size_t len = 0;
  ssize_t read;

  fp = fdopen(fd, "r");
  
  // Read one line so we can skip it.
  read = getline(&line, &len, fp);
  
  pid = fork();
  if (pid == -1) {
    perror("fork");
    exit(EXIT_FAILURE);
  }
  
  if(pid){
    // parrent 
    fclose(fp);
    close(fd);
    return pid;
  }

  // child
  
  while((read = getline(&line, &len, fp)) != -1) {
    write(1, line, read);
  }
  
  fclose(fp);
  close(fd);
  
  exit(EXIT_SUCCESS);
}

int pipeopen(char *path, char *const argv[], int *fd, int *copy_fd)
{
  int pid;
  int pipefd[2]; 
  int pipefd_copy[2];
  
  if (pipe2(pipefd, O_DIRECT) == -1) {
    perror("pipe");
    exit(EXIT_FAILURE);
  }

  if(copy_fd) {
    if (pipe2(pipefd_copy, O_DIRECT) == -1) {
      perror("pipe");
      exit(EXIT_FAILURE);
    }
  }
  
  pid = fork();

  if (pid == -1) {
    perror("fork");
    exit(EXIT_FAILURE);
  }
           
  if(pid) {
    close(pipefd[0]);
    *fd = pipefd[1]; 
    if(copy_fd) {
      close(pipefd_copy[1]);
      *copy_fd = pipefd_copy[0];
    }
    return pid;
  }

  close(pipefd[1]);
  dup2(pipefd[0], 0); 

  if(copy_fd) {
    close(pipefd_copy[0]); 
    dup2(pipefd_copy[1], 1); 
  }

  execv(path, argv); 
}

