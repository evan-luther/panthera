#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/kmem.h>

typedef struct panthera_zfs_malloc_header {
	size_t total_size;
} panthera_zfs_malloc_header_t;

void *
malloc(size_t size)
{
	panthera_zfs_malloc_header_t *header;
	size_t total_size;

	if (size == 0)
		size = 1;
	if (size > SIZE_MAX - sizeof (*header))
		return (NULL);

	total_size = size + sizeof (*header);
	header = zfs_kmem_alloc(total_size, KM_SLEEP);
	if (header == NULL)
		return (NULL);

	header->total_size = total_size;
	return (header + 1);
}

void
free(void *ptr)
{
	panthera_zfs_malloc_header_t *header;

	if (ptr == NULL)
		return;

	header = ((panthera_zfs_malloc_header_t *)ptr) - 1;
	zfs_kmem_free(header, header->total_size);
}

void *
calloc(size_t count, size_t size)
{
	void *ptr;
	size_t total_size;

	if (count != 0 && size > SIZE_MAX / count)
		return (NULL);

	total_size = count * size;
	ptr = malloc(total_size);
	if (ptr != NULL)
		memset(ptr, 0, total_size);
	return (ptr);
}
