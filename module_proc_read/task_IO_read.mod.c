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
	{ 0x457d0285, "single_open" },
	{ 0x821bc71a, "seq_printf" },
	{ 0x3f068993, "init_task" },
	{ 0x8772f0bc, "remove_proc_entry" },
	{ 0x12f5299f, "seq_read" },
	{ 0xeae94adb, "seq_lseek" },
	{ 0xfa1f5da0, "single_release" },
	{ 0xbdfb6dbb, "__fentry__" },
	{ 0xe13e0c8c, "proc_create" },
	{ 0x122c3a7e, "_printk" },
	{ 0x5b8239ca, "__x86_return_thunk" },
	{ 0xc6227e48, "module_layout" },
};

MODULE_INFO(depends, "");


MODULE_INFO(srcversion, "131F29CFE7E98DAA306CD90");
