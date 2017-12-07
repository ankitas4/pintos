#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "list.h"
#include "process.h"

//system call handler
static void syscall_handler (struct intr_frame *);

extern bool running;

void
syscall_init (void) 
{
//system call initial
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f) 
{
  int * p = f->esp;
  is_addr_valid(p);
  int system_call = * p;

  switch (system_call)
  {
		case SYS_HALT:
		  shutdown_power_off();
		break;

		case SYS_EXIT:
		  is_addr_valid(p+1);
		  thread_exit_with(*(p+1));
		break;

		case SYS_EXEC:
		  is_addr_valid(p+1);
		  is_addr_valid(*(p+1));
		  f->eax = process_execute_with_args(*(p+1));
		break;

		case SYS_WAIT:
		  is_addr_valid(p+1);
		  f->eax = process_wait(*(p+1));
		break;

		case SYS_CREATE:
		  is_addr_valid(p+5);
		  is_addr_valid(*(p+4));
		  acquire_filesys_lock();
		  f->eax = filesys_create(*(p+4),*(p+5));
		  release_filesys_lock();
		break;

		case SYS_REMOVE:
		  is_addr_valid(p+1);
		  is_addr_valid(*(p+1));
		  acquire_filesys_lock();
		  if(filesys_remove(*(p+1))==NULL)
		    f->eax = false;
		  else
		    f->eax = true;
		  release_filesys_lock();
		break;

		case SYS_OPEN:
		  is_addr_valid(p+1);
		  is_addr_valid(*(p+1));
		  acquire_filesys_lock();
		  struct file* fptr = filesys_open (*(p+1));
		  release_filesys_lock();
		  if(fptr==NULL)
		    f->eax = -1;
		  else
		  {
			struct file_info *pfile = malloc(sizeof(*pfile));
			pfile->ptr = fptr;
			pfile->fd = thread_current()->fd_num;
			thread_current()->fd_num++;
			list_push_back (&thread_current()->files, &pfile->elem);
			f->eax = pfile->fd;
		  }
		break;

		case SYS_FILESIZE:
		  is_addr_valid(p+1);
		  acquire_filesys_lock();
		  f->eax = file_length (list_search(&thread_current()->files, *(p+1))->ptr);
		  release_filesys_lock();
		break;

		case SYS_READ:
		  // 5:file name, 6:buff, 7:size	
		  is_addr_valid(p+5);
		  is_addr_valid(p+6);
		  is_addr_valid(p+7);
		  is_addr_valid(*(p+6));
		
		  acquire_filesys_lock();
		  if(*(p+5)==0)
		  {
			int i;
			//uint8_t* buffer = *(p+6);
			char *buffer = *(p+6);
			for(i=0;i<*(p+7);i++)
				*(char *)(buffer + i) = input_getc ();
			f->eax = *(p+7);
		  }
		  else
		  {
			struct file_info* fptr = list_search(&thread_current()->files, *(p+5));
			if(fptr == NULL)
				f->eax = -1;
			else
				f->eax = file_read (fptr->ptr, *(p+6), *(p+7));
		  }
		  release_filesys_lock();
		break;

		case SYS_WRITE:
		  is_addr_valid(p+7);
		  is_addr_valid(*(p+6));
		  if(*(p+5)==1)
		  {
			putbuf(*(p+6),*(p+7));
			f->eax = *(p+7);
		  }
		  else
		  {
			struct file_info* fptr = list_search(&thread_current()->files, *(p+5));
			if(fptr==NULL)
				f->eax=-1;
			else
			{
				acquire_filesys_lock();
				f->eax = file_write (fptr->ptr, *(p+6), *(p+7));
				release_filesys_lock();
			}
		  }
		break; 

		case SYS_SEEK:
		  is_addr_valid(p+5);
		  acquire_filesys_lock();
		  file_seek(list_search(&thread_current()->files, *(p+4))->ptr,*(p+5));
		  release_filesys_lock();
		break;

		case SYS_TELL:
		  is_addr_valid(p+1);
		  acquire_filesys_lock();
		  f->eax = file_tell(list_search(&thread_current()->files, *(p+1))->ptr);
		  release_filesys_lock();
		break;

		case SYS_CLOSE:
		  is_addr_valid(p+1);
		  acquire_filesys_lock();
		  close_file(&thread_current()->files,*(p+1));
		  release_filesys_lock();
		break;


		default:
		  //no system call exists
		  thread_exit_with(-1);
	}
}

int process_execute_with_args(char *file_name)
{
  //the method is use system function strtok_r to get extract the file name and invoke the process_execute()
  acquire_filesys_lock();
  char * fn_cp = malloc (strlen(file_name)+1);
  strlcpy(fn_cp, file_name, strlen(file_name)+1);
  char * save_ptr;
  fn_cp = strtok_r(fn_cp," ",&save_ptr);
  struct file* f = filesys_open (fn_cp);

  if(f == NULL)
  {
    release_filesys_lock();
    return -1; 
  }
  else
  {
    file_close(f);
    release_filesys_lock();
    return process_execute(file_name);
  }
}

void thread_exit_with(int status)
{
	//printf("Exit : %s %d %d\n",thread_current()->name, thread_current()->tid, status);
	struct list_elem *e;

      for (e = list_begin (&thread_current()->parent->children); e != list_end (&thread_current()->parent->children);
           e = list_next (e))
        {
          struct child *f = list_entry (e, struct child, elem);
          if(f->tid == thread_current()->tid)
          {
          	f->used = true;
          	f->ret_status = status;
          }
        }


	thread_current()->ret_status = status;

	if(thread_current()->parent->wait_for == thread_current()->tid)
		sema_up(&thread_current()->parent->lock_child);

	thread_exit();
}

void* is_addr_valid(const void *vaddr)
{
	if (!is_user_vaddr(vaddr))
	{
		thread_exit_with(-1);
		return 0;
	}
	void *ptr = pagedir_get_page(thread_current()->pagedir, vaddr);
	if (!ptr)
	{
		thread_exit_with(-1);
		return 0;
	}
	return ptr;
}

struct file_info* list_search(struct list* files, int fd)
{
//find files through fd, return the pointer of it
	struct list_elem *e;
        for (e = list_begin (files); e != list_end (files);
           e = list_next (e))
        {
          struct file_info *f = list_entry (e, struct file_info, elem);
          if(f->fd == fd)
          	return f;
        }
   return NULL;
}

void close_file(struct list* files, int fd)
{
// close the specific file
	struct list_elem *e;
	struct file_info *f;

        for (e = list_begin (files); e != list_end (files);
           e = list_next (e))
        {
          f = list_entry (e, struct file_info, elem);
          if(f->fd == fd)
          {
          	file_close(f->ptr);
          	list_remove(e);
          }
        }
    free(f);
}

void close_all_files(struct list* files)
{
//close all associated files
	struct list_elem *e;
	while(!list_empty(files))
	{
		e = list_pop_front(files);
		struct file_info *f = list_entry (e, struct file_info, elem);
	      	file_close(f->ptr);
	      	list_remove(e);
	      	free(f);
	}  
}
