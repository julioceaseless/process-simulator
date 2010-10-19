#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>

#define MAX_STR 256 
#define MAX_LINE 30
#define MAX_TABLE 30

#define BLOCK 0
#define READY 1
#define RUNNING 2

struct Cpu
{
  int pc;
  int pid;
  int value;
  int t_slice;
  int t_remain;
};

struct PcbTable
{
  int pid;
  int ppid;
  int pc;    // Initially started from zero.
  int value;
  int priority;
  int state;
  int t_start;
  int t_used;
  char fname[MAX_STR]; // Extended Data Member to read a program
};


static char buffer[BUFSIZ];

typedef struct Que {
  struct PcbTable ptable;
  struct Que *next; /* pointer to next element in list */
} QUE;

/* Insert into a head */
QUE *insert_head(QUE **p, struct PcbTable pt)
{
  QUE *n = (QUE *) malloc(sizeof(QUE));
  if (n == NULL) return NULL;

  n->next = *p;
  *p = n;
  n->ptable = pt;

  return n;
}

/* Enque into a tail */
QUE *enqueue(QUE **p, struct PcbTable pt)
{
  QUE *tail = *p;
  //printf("pid=%d\n", s.pid);
  if(*p == NULL) // For initial setup
    return insert_head(p, pt);

  while(tail->next != NULL) //tail refers to a tail node
    tail = tail->next;

  QUE *n = (QUE *) malloc(sizeof(QUE));
  if (n == NULL)
    return NULL;

  n->next = tail->next;
  tail->next = n;
  n->ptable = pt;  // equivalent to 'tail->next->ptable = pt;'

  return n;
}

/* Dequeue from a head */
struct PcbTable dequeue(QUE **p){
  struct PcbTable s;
  s.pid = -1;
  if (*p != NULL){
    //printf("DEQ(pid=%d)\n", (*p)->ptable.pid);
      s = (*p)->ptable;
      QUE *n = *p;
      *p = n->next;
      free(n);
      return s;
  }else{
    //printf("cannot remove, because queue is empty\n");
    return s;
  }
}

/* Show a queue */
void show(char *str, QUE *n)
{
  printf("%s", str);
  if (n == NULL){
    printf("queue is empty\n");
    return ;
  }
  while (n != NULL){
    printf("[%2d]", n->ptable.pid);
    n = n->next;
  }
  printf("\n");
  return;
}

void contextSwitch(struct Cpu *cpu, struct PcbTable *ptable){
  struct Cpu temp;
  temp.pc = cpu->pc;
  temp.pid = cpu->pid;
  temp.value = cpu->value;
  temp.t_slice = cpu->t_slice;
  temp.t_remain = cpu->t_remain;

  cpu->pc =  ptable->pc;
  cpu->pid = ptable->pid;
  cpu->value = ptable->value;
  cpu->t_slice = ptable->t_start; // ???
  cpu->t_remain = ptable->t_used; // ???

  ptable->pc = temp.pc;
  ptable->pid = temp.pid;
  ptable->ppid = 0; // ???
  ptable->value = temp.value;
  ptable->priority = 0; // ???
  ptable->state = READY; // ???
  ptable->t_start = temp.t_slice; // ???
  ptable->t_used = temp.t_remain; // ???
}

int readProgram(char *fname, char prog[][MAX_STR]){
  FILE *fp;
  char buff[MAX_STR], *pp;
  int x, y;
  
  /* Initialize prog arrays */
  for(x=0;x<MAX_LINE;x++){
    for(y=0;y<MAX_STR;y++){
      prog[x][y] = '\0'; // Not tested
    }
  }
  
  fp = fopen(fname, "r");
  if(fp == NULL){
    printf("Can't open the file: '$s'\n", fname);
    exit(1);
  }

  int i=0;
  while(1){
    pp = fgets(buff, MAX_STR, fp);
    strcpy(prog[i], buff);
    if(pp == NULL){
      break;
    }
    //printf("%s", buff);
    i++;
  }

  fclose(fp);
  return(0);
}

char **split(int *n, char *string)
{
  char **array=NULL;
  char *p=string;
  char *s;

  for(*n=0; (s = strtok(p, " ")) != NULL; (*n)++) {
    array = (char**)realloc(array, sizeof(char*) * (*n+1));
    array[*n] = s;
    p = NULL;
  }

  return array;
}

void copy(FILE *fin, FILE *fout)
{
  while (fgets(buffer, BUFSIZ, fin) != NULL) {
    fputs(buffer, fout);
    fflush(fout);
  }
}

void commanderProcess(int wfd)
{
  FILE *fp = fdopen(wfd, "w");
  int status;
  char cmd[MAX_STR];
  
  if (fp == NULL) {
    perror("parent: fdopen");
    exit(3);
  }
  
  copy(stdin, fp);
  fclose(fp);

  if (wait(&status) == -1) {
    perror("wait");
    exit(4);
  }
}

void reporterProcess(int time, QUE *s_run, QUE *s_ready, QUE *s_block){
  printf("*********************************************\n");
  printf("The current system state is as follows:\n");
  printf("*********************************************\n");
  printf("CURRENT TIME: %d\n", time);
  printf("\n");
  printf("RUNNING PROCESS:\n");
  show("Running PcbTables: ", s_run);
  //TODO: Formatting the data as following:
  //pid, ppid, priority, value, start time, CPU time used so far
  printf("\n");
  printf("BLOCKED PROCESSES:\n");
  show("Queue of blocked processes: ", s_block);
  //TODO: Formatting the data as following:
  //pid, ppid, priority, value, start time, CPU time used so far
  printf("\n");
  printf("PROCESSES READY TO EXECUTE:\n");
  show("Queue of processes with priority 0: ", s_ready);
  //TODO: Formatting the data as following:
  //pid, ppid, value, start time, CPU time used so far
  
  //show("Queue of processes with priority 1: ", s_ready);
  // ...
  //show("Queue of processes with priority 3: ", s_ready);
  printf("Terminated Reporter Process.\n");
  printf("\n");
  exit(3);
}

void processManagerProcess(int rfd)
{
  FILE *fp = fdopen(rfd, "r");
  char prog[MAX_LINE][MAX_STR];
  char **cmd;
  int i;
  int pid_cnt;
  int arg;
  int x,y; // iterators
  
  
  /* ProcessManager's 6+1 Data Structures*/
  int time;
  struct Cpu cpu;
  struct PcbTable ptable;
  QUE *ready_states;
  QUE *blocked_states;
  QUE *running_states;
  /**************************************/

  /* Initializing */
  pid_cnt = 0;
  Time = 0;
  cpu.pc = 0;
  cpu.value = 0;
  
  ptable.pid = pid_cnt++;
  ptable.ppid = -1;
  ptable.priority = 0;
  ptable.pc = 0;
  ptable.value = 0;
  ptable.t_start = time;
  ptable.t_used = time - ptable.t_start;
  //ptable.fname[] = "init";
  //strcpy("init", ptable.fname);
  strcpy(ptable.fname, "init");
  
  readProgram(ptable.fname, prog);
  
  ready_states = NULL;
  blocked_states = NULL;
  running_states = NULL; // TODO: Re-thinking if needed
  enqueue(&running_states, ptable);
  /****************/


  int n;
  struct PcbTable temp_ptable;
  int temp_value;
  int temp_pid;
  
  while (fgets(buffer, BUFSIZ, fp) != NULL) {
    printf("Instruction=%s",buffer);
    if(!strcmp(buffer, "Q\n")){
      printf("End of one unit of time.\n");
      cmd = split(&n, prog[cpu.pc]);
      printf("cmd[0]=%s\n",cmd[0]);
      
      if(!strcmp(cmd[0], "S")){
	printf("Set the value of the integer variable to %d.\n", atoi(cmd[1]));
	temp_value = cpu.value;
	cpu.value = atoi(cmd[1]);
	printf("cpu.value: %d -> %d\n", temp_value, cpu.value);
	
      }else if(!strcmp(cmd[0], "A")){
	printf("Add %d to the value of the integer variable.\n", atoi(cmd[1]));
	temp_value = cpu.value;
	cpu.value += atoi(cmd[1]);
	printf("cpu.value: %d -> %d\n", temp_value, cpu.value);
	
      }else if(!strcmp(cmd[0], "D")){
	printf("Substract %d from the value of the integer variable.\n", atoi(cmd[1]));
	temp_value = cpu.value;
	cpu.value -= atoi(cmd[1]);
	printf("cpu.value: %d -> %d\n", temp_value, cpu.value);
	
      }else if(!strcmp(cmd[0], "B\n")){
	printf("Block this simulated process.\n");
	temp_ptable = dequeue(&running_states);
	printf("pid=%d is blocked.\n", temp_ptable.pid);
	enqueue(&blocked_states, temp_ptable);
	// TODO: scheduling required.
	
      }else if(!strcmp(cmd[0], "E\n")){
	printf("Terminate this simulated process.\n");
	temp_ptable = dequeue(&running_states);
	printf("pid=%d is Terminated.\n", temp_ptable.pid);
	// TODO: scheduling required.
	
      }else if(!strcmp(cmd[0], "F")){
	printf("Create a new simulated process at %d times.\n", atoi(cmd[1]));
	arg = atoi(cmd[1]);
	// TODO: storeData(&cpu, &ptable);
	for(x=0; x<arg; x++){
  	  // TODO: create new processes.
	  printf("Created a process with pid=%d.\n", pid_cnt);
	  pid_cnt++;
	}
	
      }else if(!strcmp(cmd[0],"R")){
	printf("Replace the program of the simulated process with the program in the file '%s'.\n", cmd[1]);
	cpu.pc = 0;
	for(x=0;x<MAX_LINE;x++){
	  for(y=0;y<MAX_STR;y++){
	    //prog[x][y] = '\0'; // Not tested
	  }
	}
	//readProgram(cmd[1], prog); // Not tested
	
      }else{
	printf("Unknown Instruction.");	
      }
      
      free(cmd);
      time++;
      cpu.pc++;
      
    }else if(!strcmp(buffer, "U\n")){
      printf("Unblock the first simulated process in blocked queue.\n");
      temp_ptable = dequeue(&blocked_states);
      if(temp_ptable.pid == -1){
	printf("There are no states in blocked queue.\n");
      }else{
	printf("pid=%d moves from blocked queue to ready queue.\n", temp_ptable.pid);
	enqueue(&ready_states, temp_ptable);
      }
      
    }else if(!strcmp(buffer, "P\n")){
      printf("Print the current state of the system.\n");
      if ((temp_pid = fork()) == -1) {
	perror("fork");
      } else if (temp_pid == 0) {
	reporterProcess(time, running_states, ready_states, blocked_states);
      } else {
	// Do nohing.
      }      
    }else if(!strcmp(buffer, "T\n")){
      printf("Print the average turnaround time, and terminate the system.\n");
      return;
      
    }else{
      printf("Unknown command.\n");
    }
    //fputs(buffer, stdout);

    printf("\n");
    fflush(stdout);
  }
  fclose(fp);
}

int main(void)
{
  int rv = 0, fd[2], pid;

  if (pipe(fd)) {
    perror("pipe");
    rv = 1;
  } else if ((pid = fork()) == -1) {
    perror("fork");
    rv = 2;
  } else if (pid > 0) {
    close(fd[0]);
    commanderProcess(fd[1]);
  } else {
    close(fd[1]);
    processManagerProcess(fd[0]);
  }

  return rv;
}

