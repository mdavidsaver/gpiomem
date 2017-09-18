/**
 * Interface for /dev/mem and similar
 * which access to MMIO registers
 */
#include <Python.h>
#include <structmember.h>

#include <inttypes.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>

#include <endian.h>

#if BYTE_ORDER==LITTLE_ENDIAN
#  define ORDER (-1)
#elif BYTE_ORDER==BIG_ENDIAN
#  define ORDER (1)
#else
#  error Mixed endian not supported
#endif

typedef struct {
    PyObject_HEAD

    int fd;
    void *mbase;
    size_t msize;
} iomem;

int iomem_init(iomem *pvt, PyObject *args, PyObject *kws)
{
    const char *dname;
    unsigned long len, offset=0;
    char *names[] = {"name", "len", "offset", NULL};

    if(!PyArg_ParseTupleAndKeywords(args, kws, "sk|k", names, &dname, &len, &offset))
        return -1;

    pvt->fd = open(dname, O_RDWR|O_SYNC|O_CLOEXEC);
    if (pvt->fd<0) {
        PyErr_SetFromErrnoWithFilename(PyExc_OSError, dname);
        return -1;
    }

    pvt->msize = len;

    pvt->mbase = mmap(NULL, pvt->msize, PROT_READ|PROT_WRITE, MAP_SHARED, pvt->fd, offset);
    if (pvt->mbase==MAP_FAILED) {
        PyErr_SetFromErrnoWithFilename(PyExc_OSError, dname);
        close(pvt->fd);
        return -1;
    }
    printf("MMAP %s offset=%lx -> %p\n", dname, offset, pvt->mbase);

    return 0;
}

static
void iomem_dealloc(iomem *pvt)
{
    munmap(pvt->mbase, pvt->msize);
    close(pvt->fd);
    Py_TYPE(pvt)->tp_free(pvt);
}

static
PyObject* iomem_read(iomem *self, PyObject *args, PyObject *kws)
{
    volatile char *addr = self->mbase;
    unsigned long offset, count=1, i;
    int width=8, endian=0;
    const char *names[]  = {"offset", "count", "width", "order", NULL};

    if(!PyArg_ParseTupleAndKeywords(args, kws, "k|kii", (char**)names, &offset, &count, &width, &endian))
        return NULL;

    switch(width) {
    case 8:
    case 16:
    case 32:
        width /= 8u;
        break;
    default:
        return PyErr_Format(PyExc_ValueError, "width must be 8/16/32");
    }

    if(offset>=self->msize || width*count>self->msize-offset)
        return PyErr_Format(PyExc_ValueError, "offset and/or count would overlap");

    addr += offset;

    PyObject *ret = PyList_New(count);
    if(!ret)
        return NULL;

    for(i=0; i<count; i++) {
        volatile char *eaddr = addr+i*width;
        unsigned long val = 0;
        PyObject *num;
        __asm__ __volatile__("":::"memory");
        switch(width) {
        case 1: val = *(volatile uint8_t*)(eaddr); break;
        case 2: val = *(volatile uint16_t*)(eaddr); break;
        case 4: val = *(volatile uint32_t*)(eaddr); break;
        }
        __asm__ __volatile__("":::"memory");

        if(width>1 && endian==1) {
            switch(width) {
            case 2: val = be16toh(val); break;
            case 4: val = be32toh(val); break;
            }
        } else if(width>1 && endian==-1) {
            switch(width) {
            case 2: val = le16toh(val); break;
            case 4: val = le32toh(val); break;
            }
        }

        printf("read %p %lx\n", eaddr, val);

        num = PyLong_FromSize_t(val);
        if(!num) {
            Py_DecRef(ret);
            return NULL;
        }
        PyList_SET_ITEM(ret, i, num); // steals ref 'num'
    }

    return ret;
}

static
PyObject* iomem_write(iomem *self, PyObject *args, PyObject *kws)
{
    volatile char *addr = self->mbase, *mend = self->mbase+self->msize;
    unsigned long offset = 0;
    int width=8, endian=0;
    PyObject *values, *iter, *item;
    const char *names[]  = {"offset", "values", "width", "order", NULL};

    if(!PyArg_ParseTupleAndKeywords(args, kws, "kO|ii", (char**)names, &offset, &values, &width, &endian))
        return NULL;

    if(offset>=self->msize || width>self->msize-offset) {
        PyErr_Format(PyExc_ValueError, "out of range");
        return NULL;;
    }

    switch(width) {
    case 8:
    case 16:
    case 32:
        width /= 8u;
        break;
    default:
        return PyErr_Format(PyExc_ValueError, "width must be 8/16/32");
    }

    addr += offset;

    iter = PyObject_GetIter(values);
    if(!iter)
        return NULL;

    while((item=PyIter_Next(iter))!=NULL) {
        unsigned long val = PyNumber_AsSsize_t(item, NULL);

        if(width>1 && endian==1) {
            switch(width) {
            case 2: val = htobe16(val); break;
            case 4: val = htobe32(val); break;
            }
        } else if(width>1 && endian==-1) {
            switch(width) {
            case 2: val = htole16(val); break;
            case 4: val = htole32(val); break;
            }
        }

        if(addr>=mend) {
            PyErr_Format(PyExc_ValueError, "too many values");
            break;
        }

        printf("write %p %lx\n", addr, val);

        __asm__ __volatile__("":::"memory");
        switch(width) {
        case 1: *(volatile uint8_t*)(addr) = val; break;
        case 2: *(volatile uint16_t*)(addr) = val; break;
        case 4: *(volatile uint32_t*)(addr) = val; break;
        }
        __asm__ __volatile__("":::"memory");

        addr += width;
        Py_DecRef(item);
    }

    Py_DecRef(iter);

    if(PyErr_Occurred())
        return NULL;
    else
        Py_RETURN_NONE;
}

static
struct PyMethodDef iomem_methods[] = {
{"read", (PyCFunction)&iomem_read, METH_VARARGS|METH_KEYWORDS,
 "read(offset, count=1, width=8, order=NATIVE) -> []\n"},
{"write", (PyCFunction)&iomem_write, METH_VARARGS|METH_KEYWORDS,
 "write(offset, values, width=8, order=NATIVE)\n"},
    {NULL}
};

static PyTypeObject iomem_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "iomem._iomem.IOMEM",
    sizeof(iomem),
};

#if PY_MAJOR_VERSION >= 3
static struct PyModuleDef iomemmodule = {
  PyModuleDef_HEAD_INIT,
    "gpiomem._iomem",
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
PyMODINIT_FUNC PyInit__iomem(void)
#else
PyMODINIT_FUNC init_iomem(void)
#endif
{
    PyObject *mod = NULL;

#if PY_MAJOR_VERSION >= 3
    mod = PyModule_Create(&iomemmodule);
#else
    mod = Py_InitModule("gpiomem._iomem", NULL);
#endif

    iomem_type.tp_flags = Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE;
    iomem_type.tp_methods = iomem_methods;
    iomem_type.tp_init = (initproc)iomem_init;
    iomem_type.tp_dealloc = (destructor)iomem_dealloc;

    iomem_type.tp_new = PyType_GenericNew;
    if(PyType_Ready(&iomem_type)<0) {
        fprintf(stderr, "iomem object not ready\n");
        Py_DecRef(mod);
        MODINIT_RET(NULL);
    }

    PyObject *typeobj=(PyObject*)&iomem_type;
    Py_INCREF(typeobj);
    if(PyModule_AddObject(mod, "IOMEM", typeobj)) {
        Py_DECREF(typeobj);
        Py_DecRef(mod);
        fprintf(stderr, "Failed to add IOMEM object to module\n");
        MODINIT_RET(NULL);
    }

    PyModule_AddIntConstant(mod, "MSB", 1);
    PyModule_AddIntConstant(mod, "LSB", -1);
    PyModule_AddIntConstant(mod, "NATIVE", 0);

    MODINIT_RET(mod);
}
