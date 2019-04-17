#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <x86intrin.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct ent_ {
	void* ptr;
	size_t cur;
	size_t sz;
} ent;


#define MAX_OPENS (100)

static ent ents[MAX_OPENS];
static int entries = 0;

void* nvm_open(const char* path, size_t size)
{
	printf("sizeof: size_t:%d\n", sizeof(size_t));
	int fd = open("/dev/dax0.0", O_RDWR);
	size = (size + (1024*1024-1)) & ~(1024*1024-1);
	size_t giga = size >> 31;
	size_t mod = size & (2048ULL*1024*1024-1);
	printf("size:%llu -> 2GigaBytePages:%llu mod:%llu\n", size, giga, mod);
	void* addr = NULL;
	size_t offs = 0;
	void* p = mmap(addr, size < 0x80000000ULL ? size : 0x80000000ULL, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);

	perror("mmap");
	printf("mapped address: %p\n", p);

	addr = p;
	for (int i = 0; i < giga; i ++) {
		//void* pp = mmap(addr, size < 0x80000000ULL ? size : 0x80000000ULL, PROT_READ|PROT_WRITE, MAP_SHARED, fd, offs);
		void* pp = mmap(addr, size < 0x80000000ULL ? size : 0x80000000ULL, PROT_READ|PROT_WRITE, MAP_FIXED|MAP_SHARED, fd, offs);
		addr = ((char*)addr) + (2048*1024*1024);
		offs = offs + (2048*1024*1024);
		perror("mmap");
		if (!p) p = pp;
		printf("mapped address: %p\n", pp);
	}
	
	printf("nvm_open: fd:%d size:%zu addr:%p\n", fd, size, p);

	ent* ret = NULL;
	for (int i = 0; i < MAX_OPENS; i ++) {
		if (ents[i].ptr == NULL) {
			ret = &ents[i];
			break;
		}
	}
	ret->ptr = p;
	ret->cur = 0ULL;
	ret->sz = size;
	entries ++;
	return ret;
}

void* nvm_split(void* h, size_t pos)
{
	ent* e = (ent*)h;
	if (!e || !e->ptr)
		return NULL;
	ent* ret = NULL;
	for (int i = 0; i < MAX_OPENS; i ++) {
		if (ents[i].ptr == NULL) {
			ret = &ents[i];
			break;
		}
	}
	if (!ret)
		return NULL;

	ret->ptr = (char*)(e->ptr) + pos;
	ret->cur = 0ULL;
	ret->sz = e->sz - pos;
	entries ++;
	return ret;
}

ssize_t nvm_position(void* h)
{
	ent* e = (ent*)h;
	if (!e || !e->ptr)
		return -EINVAL;
	return ((ent*)h)->cur;
}

ssize_t nvm_size(void* h)
{
	ent* e = (ent*)h;
	if (!e || !e->ptr)
		return -EINVAL;
	return ((ent*)h)->sz;
}

off_t nvm_lseek(void* h, off_t offset, int whence)
{
	ent* e = (ent*)h;
	if (!e || !e->ptr)
		return -EINVAL;
	e->cur = offset;
	return offset;
}

ssize_t nvm_write(void* h, const char* buf, size_t len)
{
	ent* e = (ent*)h;
	if (!e || !e->ptr)
		return -EINVAL;
	size_t cur = e->cur;
	char* p = (char*)e->ptr;
	size_t left = e->sz - e->cur;
	len = left < len ? left : len;
	memcpy(p + cur, buf, len);

	for (size_t i = 0; i < len; i += 64) {
		_mm_clflushopt((void*)((((uint64_t)p + cur + i) + 63) & ~63));
	}
	e->cur += len;

	_mm_sfence();
	return len;
}

ssize_t nvm_read(void* h, char* buf, size_t len)
{
	ent* e = (ent*)h;
	if (!e || !e->ptr)
		return -EINVAL;
	size_t cur = e->cur;
	char* p = (char*)e->ptr;

	_mm_sfence();

	size_t left = e->sz - e->cur;
	len = left < len ? left : len;
	memcpy(buf, p + cur, len);
	e->cur += len;
	return len;
}
	
void nvm_close(void* h)
{
	ent* e = (ent*)h;
	if (!e || !e->ptr) 
		return;
	e->cur = 0;
	e->ptr = NULL;
	entries --;
}

#if defined(__cplusplus)
}
#endif