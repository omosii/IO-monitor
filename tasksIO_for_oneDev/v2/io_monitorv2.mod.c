#include <linux/module.h>
#define INCLUDE_VERMAGIC
#include <linux/build-salt.h>
#include <linux/elfnote-lto.h>
#include <linux/export-internal.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

#ifdef CONFIG_UNWINDER_ORC
#include <asm/orc_header.h>
ORC_HEADER;
#endif

BUILD_SALT;
BUILD_LTO_INFO;

MODULE_INFO(vermagic, VERMAGIC_STRING);
MODULE_INFO(name, KBUILD_MODNAME);

__visible struct module __this_module
__section(".gnu.linkonce.this_module") = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};

#ifdef CONFIG_RETPOLINE
MODULE_INFO(retpoline, "Y");
#endif



static const struct modversion_info ____versions[]
__used __section("__versions") = {
	{ 0x87a21cb3, "__ubsan_handle_out_of_bounds" },
	{ 0xbb10e61d, "unregister_kprobe" },
	{ 0x8772f0bc, "remove_proc_entry" },
	{ 0x44c10a52, "kvfree_call_rcu" },
	{ 0x122c3a7e, "_printk" },
	{ 0x71ba2490, "pcpu_hot" },
	{ 0x8d522714, "__rcu_read_lock" },
	{ 0x2469810f, "__rcu_read_unlock" },
	{ 0x4c03a563, "random_kmalloc_seed" },
	{ 0x37a99944, "kmalloc_caches" },
	{ 0x22e14f04, "kmalloc_trace" },
	{ 0x50ba7ec1, "__get_task_comm" },
	{ 0xba8fbd64, "_raw_spin_lock" },
	{ 0xb5b54b34, "_raw_spin_unlock" },
	{ 0x54b1fac6, "__ubsan_handle_load_invalid_value" },
	{ 0x6091797f, "synchronize_rcu" },
	{ 0x1e5f3fc5, "proc_create_single_data" },
	{ 0x3f66a26e, "register_kprobe" },
	{ 0xbdfb6dbb, "__fentry__" },
	{ 0x821bc71a, "seq_printf" },
	{ 0x626c45d3, "seq_puts" },
	{ 0x5b8239ca, "__x86_return_thunk" },
	{ 0xc6227e48, "module_layout" },
};

MODULE_INFO(depends, "");


MODULE_INFO(srcversion, "8EFA8CFDA5127889E5719A9");
