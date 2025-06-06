/*
 * Copyright (c) 2006-2018, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2006-03-23     Bernard      the first version
 * 2010-11-10     Bernard      add cleanup callback function in thread exit.
 * 2012-12-29     Bernard      fix compiling warning.
 * 2013-12-21     Grissiom     let rt_thread_idle_excute loop until there is no
 *                             dead thread.
 * 2016-08-09     ArdaFu       add method to get the handler of the idle thread.
 * 2018-02-07     Bernard      lock scheduler to protect tid->cleanup.
 * 2018-07-14     armink       add idle hook list
 */

#include <rthw.h>
#include <rtthread.h>

#ifdef RT_USING_MODULE
    #include <dlmodule.h>
#endif

#if defined (RT_USING_HOOK)
    #ifndef RT_USING_IDLE_HOOK
        #define RT_USING_IDLE_HOOK
    #endif
#endif

#ifndef IDLE_THREAD_STACK_SIZE
    #if defined (RT_USING_IDLE_HOOK) || defined(RT_USING_HEAP)
        #define IDLE_THREAD_STACK_SIZE  256
    #else
        #define IDLE_THREAD_STACK_SIZE  128
    #endif
#endif

__ROM_USED struct rt_thread idle;
ALIGN(RT_ALIGN_SIZE)
__ROM_USED rt_uint8_t idle_thread_stack[IDLE_THREAD_STACK_SIZE];

extern rt_list_t rt_thread_defunct;

#ifdef RT_USING_IDLE_HOOK

#ifndef RT_IDEL_HOOK_LIST_SIZE
    #define RT_IDEL_HOOK_LIST_SIZE          4
#endif

__ROM_USED void (*idle_hook_list[RT_IDEL_HOOK_LIST_SIZE])();

/**
 * @ingroup Hook
 * This function sets a hook function to idle thread loop. When the system performs
 * idle loop, this hook function should be invoked.
 *
 * @param hook the specified hook function
 *
 * @return RT_EOK: set OK
 *         -RT_EFULL: hook list is full
 *
 * @note the hook function must be simple and never be blocked or suspend.
 */
__ROM_USED rt_err_t rt_thread_idle_sethook(void (*hook)(void))
{
    rt_size_t i;
    rt_base_t level;
    rt_err_t ret = -RT_EFULL;

    /* disable interrupt */
    level = rt_hw_interrupt_disable();

    for (i = 0; i < RT_IDEL_HOOK_LIST_SIZE; i++)
    {
        if (idle_hook_list[i] == RT_NULL)
        {
            idle_hook_list[i] = hook;
            ret = RT_EOK;
            break;
        }
    }
    /* enable interrupt */
    rt_hw_interrupt_enable(level);

    return ret;
}

/**
 * delete the idle hook on hook list
 *
 * @param hook the specified hook function
 *
 * @return RT_EOK: delete OK
 *         -RT_ENOSYS: hook was not found
 */
__ROM_USED rt_err_t rt_thread_idle_delhook(void (*hook)(void))
{
    rt_size_t i;
    rt_base_t level;
    rt_err_t ret = -RT_ENOSYS;

    /* disable interrupt */
    level = rt_hw_interrupt_disable();

    for (i = 0; i < RT_IDEL_HOOK_LIST_SIZE; i++)
    {
        if (idle_hook_list[i] == hook)
        {
            idle_hook_list[i] = RT_NULL;
            ret = RT_EOK;
            break;
        }
    }
    /* enable interrupt */
    rt_hw_interrupt_enable(level);

    return ret;
}

#endif

/* Return whether there is defunctional thread to be deleted. */
rt_inline int _has_defunct_thread(void)
{
    /* The rt_list_isempty has prototype of "int rt_list_isempty(const rt_list_t *l)".
     * So the compiler has a good reason that the rt_thread_defunct list does
     * not change within rt_thread_idle_excute thus optimize the "while" loop
     * into a "if".
     *
     * So add the volatile qualifier here. */
    const volatile rt_list_t *l = (const volatile rt_list_t *)&rt_thread_defunct;

    return l->next != l;
}

/**
 * @ingroup Thread
 *
 * This function will perform system background job when system idle.
 */
__ROM_USED void rt_thread_idle_excute(void)
{
    /* Loop until there is no dead thread. So one call to rt_thread_idle_excute
     * will do all the cleanups. */
    while (_has_defunct_thread())
    {
        rt_base_t lock;
        rt_thread_t thread;
#ifdef RT_USING_MODULE
        struct rt_dlmodule *module = RT_NULL;
#endif
        RT_DEBUG_NOT_IN_INTERRUPT;

        /* disable interrupt */
        lock = rt_hw_interrupt_disable();

        /* re-check whether list is empty */
        if (_has_defunct_thread())
        {
            /* get defunct thread */
            thread = rt_list_entry(rt_thread_defunct.next,
                                   struct rt_thread,
                                   tlist);
#ifdef RT_USING_MODULE
            module = (struct rt_dlmodule *)thread->module_id;
            if (module)
            {
                dlmodule_destroy(module);
            }
#endif
            /* remove defunct thread */
            rt_list_remove(&(thread->tlist));

            /* lock scheduler to prevent scheduling in cleanup function. */
            rt_enter_critical();

            /* invoke thread cleanup */
            if (thread->cleanup != RT_NULL)
                thread->cleanup(thread);

#ifdef RT_USING_SIGNALS
            rt_thread_free_sig(thread);
#endif

            /* if it's a system object, not delete it */
            if (rt_object_is_systemobject((rt_object_t)thread) == RT_TRUE)
            {
                /* detach this object */
                rt_object_detach((rt_object_t)thread);
                /* unlock scheduler */
                rt_exit_critical();

                /* enable interrupt */
                rt_hw_interrupt_enable(lock);

                return;
            }

            /* unlock scheduler */
            rt_exit_critical();
        }
        else
        {
            /* enable interrupt */
            rt_hw_interrupt_enable(lock);

            /* may the defunct thread list is removed by others, just return */
            return;
        }

        /* enable interrupt */
        rt_hw_interrupt_enable(lock);

#ifdef RT_USING_HEAP
        /* release thread's stack */
        RT_KERNEL_FREE(thread->stack_addr);
        /* delete thread object */
        rt_object_delete((rt_object_t)thread);
#endif
    }
}

extern void rt_system_power_manager(void);
static void rt_thread_idle_entry(void *parameter)
{
    while (1)
    {

#ifdef RT_USING_IDLE_HOOK
        rt_size_t i;

        for (i = 0; i < RT_IDEL_HOOK_LIST_SIZE; i++)
        {
            if (idle_hook_list[i] != RT_NULL)
            {
                idle_hook_list[i]();
            }
        }
#endif

        rt_thread_idle_excute();
#ifdef RT_USING_PM
        rt_system_power_manager();
#else
#ifndef _MSC_VER
        rt_hw_cpu_idle();
#endif  /* !_MSC_VER */
#endif
    }
}

/**
 * @ingroup SystemInit
 *
 * This function will initialize idle thread, then start it.
 *
 * @note this function must be invoked when system init.
 */
__ROM_USED void rt_thread_idle_init(void)
{
    /* initialize thread */
    rt_thread_init(&idle,
                   "tidle",
                   rt_thread_idle_entry,
                   RT_NULL,
                   &idle_thread_stack[0],
                   sizeof(idle_thread_stack),
                   RT_THREAD_PRIORITY_MAX - 1,
                   32);

    /* startup */
    rt_thread_startup(&idle);
}

/**
 * @ingroup Thread
 *
 * This function will get the handler of the idle thread.
 *
 */
__ROM_USED rt_thread_t rt_thread_idle_gethandler(void)
{
    return (rt_thread_t)(&idle);
}
