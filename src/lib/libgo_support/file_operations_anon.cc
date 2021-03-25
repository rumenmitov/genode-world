/*
 * \brief  libc file operations for anon mmap
 * \author Alexander Tormasov
 * \date   2010-01-21
 */

/* Genode includes */
#include <base/env.h>
#include <os/path.h>
#include <util/token.h>

/* Genode-specific libc interfaces */
#include <libc-plugin/plugin_registry.h>
#include <libc-plugin/plugin.h>

extern "C" {
/* libc includes */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/cdefs.h>
}

/* libc-internal includes */
#include <internal/file.h>
#include <internal/mmap_registry.h>
#include <internal/init.h>

using namespace Libc;

#define __SYS_(ret_type, name, args, body) \
	extern "C" {\
	ret_type  __sys_##name args body \
	ret_type __libc_##name args __attribute__((alias("__sys_" #name))); \
	ret_type       _##name args __attribute__((alias("__sys_" #name))); \
	ret_type          name args __attribute__((alias("__sys_" #name))); \
	}

extern Libc::Mmap_registry *Libc::mmap_registry();

namespace Libc {

	void anon_mmap_construct(Genode::Env &env, size_t default_size);
	void anon_init_file_operations(Genode::Env &env,
								   Xml_node const &config_accessor);
}

static unsigned int  _mmap_align_log2 { PAGE_SHIFT };

void Libc::anon_init_file_operations(Genode::Env &env,
									 Xml_node const &config_accessor)
{
	/* by default 15 Mb for anon mmap allocator without predefined address */
	enum { DEFAULT_SIZE = 15ul * 1024 * 1024 };
	size_t default_size = DEFAULT_SIZE;

	config_accessor.with_sub_node("libc", [&] (Xml_node libc) {
		    libc.with_sub_node("mmap", [&] (Xml_node mmap) {
				_mmap_align_log2 = mmap.attribute_value("align_log2",
			                                            (unsigned int)PAGE_SHIFT);
				default_size = mmap.attribute_value("local_area_default_size",
													(size_t)DEFAULT_SIZE);
		                                              });
	});

	anon_mmap_construct(env, default_size);
}

/***************
 ** Utilities **
 ***************/


namespace Genode {

	char *pd_reserve_memory(size_t bytes, void *requested_addr,
	                        size_t alignment_hint);
	bool pd_unmap_memory(void *addr, size_t bytes, bool &area_used);
	bool pd_commit_memory(void *addr, size_t size, bool exec, bool with_requested_addr);
	void *pd_get_base_address(void *addr, bool &anon, size_t &size);
}

extern "C" {

	void *__sys_anon_mmap(void *addr, ::size_t length, int prot, int flags, int libc_fd, ::off_t offset);
}

__SYS_(void *, anon_mmap, (void *addr, ::size_t length,
                      int prot, int flags,
                      int libc_fd, ::off_t offset),
{
	if (!((flags & MAP_ANONYMOUS) || (flags & MAP_ANON)))
		return __sys_anon_mmap(addr, length, prot, flags, libc_fd, offset);

	/* handle requests only for anonymous memory */
	bool const executable = prot & PROT_EXEC;

	/* FIXME do not allow overlap with other areas as in original mmap() - just fail */
	/* desired address given as addr (mandatory if flags has MAP_FIXED) */
	void *start = Genode::pd_reserve_memory(length, addr, _mmap_align_log2);
	if (!start || ((flags & MAP_FIXED) && (start != addr))) {
		errno = ENOMEM;
		return MAP_FAILED;
	}
	mmap_registry()->insert(start, length, 0);

	if (prot == PROT_NONE) {

		/* process request for memory range reservation (no access, no commit) */
			return start;
	}

	/* desired address returned; commit virtual range */
	Genode::pd_commit_memory(start, length, executable, addr != 0);

	/* zero commited ram */
	::memset(start, 0, align_addr(length, PAGE_SHIFT));
	return start;
})

extern "C" int anon_munmap(void *base, ::size_t length)
{
	bool nanon;
	size_t size;
	void *start = Genode::pd_get_base_address(base, nanon, size);
	if (!start)
		start = base;
	if (nanon && !mmap_registry()->registered(start)) {
		warning("munmap: could not lookup plugin for address ", start);
		errno = EINVAL;
		return -1;
	}

	/*
	 * Lookup plugin that was used for mmap
	 *
	 * If the pointer is NULL, 'start' refers to an anonymous mmap.
	 */
	Plugin *plugin = nanon ? mmap_registry()->lookup_plugin_by_addr(start) : 0;

	/*
	 * Remove registry entry before unmapping to avoid double insertion error
	 * if another thread gets the same start address immediately after unmapping.
	 */
	mmap_registry()->remove(start);

	int ret = 0;
	if (plugin) {
		ret = plugin->munmap(start, length);
	} else {
		bool area_used = false;
		Genode::pd_unmap_memory(base, length, area_used);
		/* if we should not remove registry - reinsert it;
		 * this could happens if we split internal area;
		 * size should be original, not length of current area
		 */
		if (!(nanon && !area_used))
			mmap_registry()->insert(start, size, 0);
	}
	return ret;
}

__SYS_(int, anon_msync, (void *start, ::size_t len, int flags),
{
	if (!mmap_registry()->registered(start))
	{
		warning("munmap: could not lookup plugin for address ", start);
		errno = EINVAL;
		return -1;
	}

	/*
	 * Lookup plugin that was used for mmap
	 *
	 * If the pointer is NULL, 'start' refers to an anonymous mmap.
	 */
	Plugin *plugin = mmap_registry()->lookup_plugin_by_addr(start);

	int ret = 0;
	if (plugin)
		ret = plugin->msync(start, len, flags);

	return ret;
})
