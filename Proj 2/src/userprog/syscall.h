#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "list.h"

void syscall_init (void);

struct file_info {
// make this structure listable and store pointer and file descriptor
	struct file* ptr;
	int fd;
	struct list_elem elem;
};


// check if address valid (user_addr, kernel_addr)
void* is_addr_valid(const void*);

struct file_info* list_search(struct list* files, int fd);



#endif /* userprog/syscall.h */
