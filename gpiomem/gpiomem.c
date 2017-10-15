/**
 * Interface for /dev/gpiomem which exposes the GPIO control
 * registers on the BCM2835/7
 */
#include <Python.h>
#include <structmember.h>

#include <string.h>
#include <inttypes.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>

// N <= 5
#define R_GPFSEL(N) (0x0 + 4u*(N))

// N <= 1
#define R_GPSET(N) (0x1Cu + 4u*(N))
#define R_GPCLR(N) (0x28u + 4u*(N))
#define R_GPLEV(N) (0x34u + 4u*(N))

typedef struct {
    PyObject_HEAD

    int fd;
    void *mbase;
    size_t msize;

    unsigned int npins;
} gpiomem;

static
uint32_t ioread32(void *addr)
{
    volatile uint32_t *iptr = addr;
    uint32_t ret;
    __asm__ __volatile__("":::"memory");
    ret = *iptr;
    __asm__ __volatile__("":::"memory");
    return ret;
}

static
void iowrite32(void *addr, uint32_t val)
{
    volatile uint32_t *iptr = addr;
    __asm__ __volatile__("":::"memory");
    *iptr = val;
    __asm__ __volatile__("":::"memory");
}

static
int gpiomem_init(gpiomem *pvt, PyObject *args, PyObject *kws)
{
    const char *dname = "/dev/gpiomem";
    char *names[] = {"name", NULL};

    if(!PyArg_ParseTupleAndKeywords(args, kws, "|s", names, &dname))
        return -1;

    pvt->npins = 54;

    pvt->fd = open(dname, O_RDWR|O_SYNC|O_CLOEXEC);
    if (pvt->fd<0) {
        PyErr_SetFromErrnoWithFilename(PyExc_OSError, dname);
        return -1;
    }

    pvt->msize = 0x100;

    pvt->mbase = mmap(NULL, pvt->msize, PROT_READ|PROT_WRITE, MAP_SHARED, pvt->fd, 0);
    if (pvt->mbase==MAP_FAILED) {
        PyErr_SetFromErrnoWithFilename(PyExc_OSError, dname);
        close(pvt->fd);
        return -1;
    }

    return 0;
}

static
void gpiomem_dealloc(gpiomem *pvt)
{
    munmap(pvt->mbase, pvt->msize);
    close(pvt->fd);
    Py_TYPE(pvt)->tp_free(pvt);
}

typedef int (*pinfn)(gpiomem *, unsigned, void *);

static
int foreachpin(gpiomem *pvt, PyObject *pseq, pinfn fn, void *pfn)
{
    PyObject *iter = PyObject_GetIter(pseq), *next;
    if(iter==NULL)
        return -1;

    while((next=PyIter_Next(iter))!=NULL)
    {
        Py_ssize_t ret = PyNumber_AsSsize_t(next, NULL);
        Py_DECREF(next);

        if(ret<0 || ret>=pvt->npins) {
            PyErr_SetObject(PyExc_ValueError, pseq);
            Py_DECREF(iter);
            return -1;
        }

        if((*fn)(pvt, ret, pfn)) {
            Py_DECREF(iter);
            return -1;
        }
    }

    Py_DECREF(iter);

    if(PyErr_Occurred())
        return -1;

    return 0;
}

typedef struct {
    /* GPFSEL# registers have 3 bits per pin, 10 pins and 30 bits per register */
    uint64_t rval[6];
    uint64_t mask[6];
    PyObject *ret;
} altinfo;

static
int gpiomem_getalt_pin(gpiomem *pvt, unsigned pin, void *pinfo)
{
    altinfo *info = pinfo;
    unsigned nreg = pin/10u, nbit = pin%10u;
    uint32_t val = info->rval[nreg]>>(3u*nbit);
    PyObject *num = PyInt_FromLong(val&7u);

    if(!num)
        return -1;

    if(PyList_Append(info->ret, num)) {
        Py_DECREF(num);
        return -1;
    }
    return 0;
}

static
PyObject *gpiomem_getalt(gpiomem *pvt, PyObject *args)
{
    PyObject *pseq;
    altinfo info;
    unsigned i;

    if(!PyArg_ParseTuple(args, "O", &pseq))
        return NULL;

    info.ret = PyList_New(0);
    if(!info.ret)
        return NULL;

    for(i=0; i<6; i++)
        info.rval[i] = ioread32(pvt->mbase + R_GPFSEL(i));

    if(foreachpin(pvt, pseq, &gpiomem_getalt_pin, &info)) {
        Py_DECREF(info.ret);
        return NULL;
    }

    return info.ret;
}

static
int gpiomem_setalt_pin(gpiomem *pvt, unsigned pin, void *pinfo)
{
    altinfo *info = pinfo;
    PyObject *num = PyIter_Next(info->ret);
    Py_ssize_t val;
    unsigned nreg = pin/10u, nbit = pin%10u;

    if(!num)
        return -1;

    val = PyNumber_AsSsize_t(num, NULL);
    if(val<0 || val>7) {
        PyErr_SetObject(PyExc_ValueError, num);
        Py_DECREF(num);
        return -1;
    }
    Py_DECREF(num);

    info->mask[nreg] |= 7u<<(3*nbit);
    info->rval[nreg] |= val<<(3*nbit);
    return 0;
}

static
PyObject *gpiomem_setalt(gpiomem *pvt, PyObject *args)
{
    PyObject *pseq, *vals;
    altinfo info;
    unsigned i;

    if(!PyArg_ParseTuple(args, "OO", &pseq, &vals))
        return NULL;

    info.ret = PyObject_GetIter(vals);
    if(!info.ret)
        return NULL;

    memset(info.mask, 0, sizeof(info.mask));
    memset(info.rval, 0, sizeof(info.rval));

    if(foreachpin(pvt, pseq, &gpiomem_setalt_pin, &info)) {
        Py_DECREF(info.ret);
        return NULL;
    }
    Py_DECREF(info.ret);

    for(i=0; i<6; i++)
    {
        uint32_t val;
        if(!info.mask[i]) continue;
        val  = ioread32(pvt->mbase + R_GPFSEL(i)) & ~info.mask[i];
        val |= info.rval[i] & info.mask[i];
        iowrite32(pvt->mbase + R_GPFSEL(i), val);
    }

    Py_RETURN_NONE;
}

typedef struct {
    uint32_t smask[2], // bits to set
             cmask[2], // bits to clear
             cur[2];   // current levels
    PyObject *ret,
             *in;
} outinfo;

static
int gpiomem_out_pin(gpiomem *pvt, unsigned pin, void *pinfo)
{
    outinfo *info = pinfo;
    PyObject *num = PyIter_Next(info->in);
    unsigned nreg = pin/32u, nbit = pin%32u;

    if(!num)
        return -1;

    if(num!=Py_None) {

        Py_ssize_t val = PyNumber_AsSsize_t(num, NULL);

        if(val!=0)
            info->smask[nreg] |= 1<<nbit;
        else
            info->cmask[nreg] |= 1<<nbit;
    }
    Py_DECREF(num);

    num = PyInt_FromLong(!!(info->cur[nreg]&(1<<nbit)));
    if(!num)
        return -1;

    if(PyList_Append(info->ret, num)) {
        Py_DECREF(num);
        return -1;
    }
    Py_DECREF(num);

    return 0;
}

static
PyObject *gpiomem_out(gpiomem *pvt, PyObject *args)
{
    PyObject *pseq, *vals;
    outinfo info;

    memset(&info, 0, sizeof(info));

    if(!PyArg_ParseTuple(args, "OO", &pseq, &vals))
        return NULL;

    info.ret = PyList_New(0);
    if(!info.ret)
        goto err;

    info.in = PyObject_GetIter(vals);
    if(!info.in)
        goto err;

    memset(info.smask, 0, sizeof(info.smask));
    memset(info.cmask, 0, sizeof(info.cmask));

    info.cur[0] = ioread32(pvt->mbase + R_GPLEV(0));
    info.cur[1] = ioread32(pvt->mbase + R_GPLEV(1));

    if(foreachpin(pvt, pseq, &gpiomem_out_pin, &info))
        goto err;

    Py_DECREF(info.in);
    info.in = NULL;

    iowrite32(pvt->mbase + R_GPSET(0), info.smask[0]);
    iowrite32(pvt->mbase + R_GPSET(1), info.smask[1]);
    iowrite32(pvt->mbase + R_GPCLR(0), info.cmask[0]);
    iowrite32(pvt->mbase + R_GPCLR(1), info.cmask[1]);

    return info.ret;
err:
    Py_XDECREF(info.in);
    Py_XDECREF(info.ret);
    return NULL;
}

static
int gpiomem_in_pin(gpiomem *pvt, unsigned pin, void *pinfo)
{
    outinfo *info = pinfo;
    unsigned nreg = pin/32u, nbit = pin%32u;
    int val = info->cmask[nreg]&(1<<nbit);

    PyObject *num = PyInt_FromLong(!!val);
    if(!num)
        return -1;
    if(PyList_Append(info->ret, num)) {
        Py_DECREF(num);
        return -1;
    }
    Py_DECREF(num);
    return 0;
}

static
PyObject *gpiomem_in(gpiomem *pvt, PyObject *args)
{
    PyObject *pseq;
    outinfo info;

    if(!PyArg_ParseTuple(args, "O", &pseq))
        return NULL;

    info.ret = PyList_New(0);
    if(!info.ret)
        return NULL;

    info.cmask[0] = ioread32(pvt->mbase + R_GPLEV(0));
    info.cmask[1] = ioread32(pvt->mbase + R_GPLEV(1));

    if(foreachpin(pvt, pseq, &gpiomem_in_pin, &info)) {
        Py_DECREF(info.ret);
        return NULL;
    }

    return info.ret;
}

static
PyObject *gpiomem__sync(gpiomem *pvt)
{
    int ret = msync(pvt->mbase, pvt->msize, MS_SYNC|MS_INVALIDATE);
    if(ret==-1) {
        PyErr_SetFromErrno(PyExc_OSError);
        return NULL;
    }
    Py_RETURN_NONE;
}

static
struct PyMemberDef gpiomem_members[] = {
    {"npins", T_UINT, offsetof(gpiomem, npins), READONLY, "Number of I/O pins"},
    {NULL}
};

static
struct PyMethodDef gpiomem_methods[] = {
{"getalt", (PyCFunction)&gpiomem_getalt, METH_VARARGS,
 "getalt([pin#, ...]) -> [mode, ...]\n"
 "Fetch current alt function assignments"},
{"setalt", (PyCFunction)&gpiomem_setalt, METH_VARARGS,
 "setalt([pin#, ...], [mode, ...])\n"
 "Change current alt function assignments.\n"
 "One of IN|OUT|ALT0|ALT1|ALT2|ALT3|ALT4|ALT5"},
{"output", (PyCFunction)&gpiomem_out, METH_VARARGS,
 "output([pin#, ...], [value, ...])\n"
 "Set output pins"},
{"input", (PyCFunction)&gpiomem_in, METH_VARARGS,
 "input([pin#, ...]) -> [mode, ...]\n"
 "Read input pins"},
{"_sync", (PyCFunction)&gpiomem__sync, METH_VARARGS,
 "msync() the underlying file mapping"},
    {NULL}
};

static PyTypeObject gpiomem_type = {
#if PY_MAJOR_VERSION >= 3
    PyVarObject_HEAD_INIT(NULL, 0)
#else
    PyObject_HEAD_INIT(NULL)
    0,
#endif
    "gpiomem._gpiomem.GPIOMEM",
    sizeof(gpiomem),
};

#if PY_MAJOR_VERSION >= 3
static struct PyModuleDef gpiomemmodule = {
  PyModuleDef_HEAD_INIT,
    "gpiomem._gpiomem",
    NULL,
    -1,
    NULL
};
#endif

#if PY_MAJOR_VERSION >= 3
# define MODINIT_RET(VAL) return (VAL)
#else
# define MODINIT_RET(VAL) return
#endif

#if PY_MAJOR_VERSION >= 3
PyMODINIT_FUNC PyInit__gpiomem(void)
#else
PyMODINIT_FUNC init_gpiomem(void)
#endif
{
    PyObject *mod = NULL;

#if PY_MAJOR_VERSION >= 3
    mod = PyModule_Create(&gpiomemmodule);
#else
    mod = Py_InitModule("gpiomem._gpiomem", NULL);
#endif

    gpiomem_type.tp_flags = Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE;
    gpiomem_type.tp_members = gpiomem_members;
    gpiomem_type.tp_methods = gpiomem_methods;
    gpiomem_type.tp_init = (initproc)gpiomem_init;
    gpiomem_type.tp_dealloc = (destructor)gpiomem_dealloc;

    gpiomem_type.tp_new = PyType_GenericNew;
    if(PyType_Ready(&gpiomem_type)<0) {
        Py_DecRef(mod);
        fprintf(stderr, "gpiomem object not ready\n");
        MODINIT_RET(NULL);
    }

    PyObject *typeobj=(PyObject*)&gpiomem_type;
    Py_INCREF(typeobj);
    if(PyModule_AddObject(mod, "GPIOMEM", typeobj)) {
        Py_DECREF(typeobj);
        Py_DecRef(mod);
        fprintf(stderr, "Failed to add GPIOMEM object to module\n");
        MODINIT_RET(NULL);
    }

    PyModule_AddIntConstant(mod, "IN"  , 0);
    PyModule_AddIntConstant(mod, "OUT" , 1);
    PyModule_AddIntConstant(mod, "ALT0", 4);
    PyModule_AddIntConstant(mod, "ALT1", 5);
    PyModule_AddIntConstant(mod, "ALT2", 6);
    PyModule_AddIntConstant(mod, "ALT3", 7);
    PyModule_AddIntConstant(mod, "ALT4", 3);
    PyModule_AddIntConstant(mod, "ALT5", 2);

    MODINIT_RET(mod);
}
