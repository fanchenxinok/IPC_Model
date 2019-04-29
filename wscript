#! /usr/bin/env python
# encoding: utf-8

APPNAME = 'waf_learn'
VERSION = '1.0.0'

top = '.'
out = 'build'

def options(opt):
        opt.load('compiler_c compiler_cxx')       

def configure(ctx):
	print(' ---- Start configure ----')
	ctx.load('compiler_c compiler_cxx')

	# 查找应用并赋值给环境变量: 实现 CC=XXX/gcc XX=g++
	ctx.find_program('gcc', var='CC')
	ctx.find_program('g++', var='XX')
	ctx.find_program('ar', var='AR')

	print 'CC = %s' % ctx.env['CC'][0]
	print 'CXX = %s' % ctx.env.CXX
	print 'AR = %s' % ctx.env.AR

	# CFLAGS 定义
	#ctx.env.CFLAGS = ['-g']  # 等价于 ctx.env['CFLAGS'] = ['-g']
	#ctx.env.append_value('CFLAGS', ['-O0', '-Wall', '-fPIC'])
	#print 'CFLAGS = %s' % ctx.env.CFLAGS

	# CLIBS 定义
	#ctx.env.CLIBS = ['-lSendMsgModel', '-lpthread', '-lrt']
	#print 'CLIBS = %s' % ctx.env.CLIBS

	# BUILD_USELIB
	#ctx.env.BUILD_USELIB = []

	#ARFLAG
	#ctx.env.ARFLAG = ['-rcs']
	#print 'ARFLAG = %s' % ctx.env.ARFLAG

#定义在编译前执行的函数
def build_before(ctx):
	print 'do something before building the project.'

#定义在编译完成后执行的函数
def build_after(ctx):
	print 'do something after building the project.'


def build(bld):
	print('++++ build %s' % bld.path.abspath())

	bld.add_pre_fun(build_before) #添加编译前需要执行的函数
	bld.add_post_fun(build_after) #添加编译后需要执行的函数

	bld.recurse('server')
	bld.recurse('test')
	bld.recurse('log_recv')
