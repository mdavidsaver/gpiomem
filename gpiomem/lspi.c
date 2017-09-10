
#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <structmember.h>

#include <inttypes.h>

#include <linux/spi/spidev.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

typedef struct {
    PyObject_HEAD

    int fd;
    unsigned mode;
    unsigned speed;

} spidev;

static
int spidev_init(spidev *pvt, PyObject *args, PyObject *kws)
{
    char buf[32];
    const char *dname = NULL;
    unsigned int bus=0, dev=0;

    char *names[] = {"bus", "device", "name", NULL};

    if(!PyArg_ParseTupleAndKeywords(args, kws, "|IIz", names, &bus, &dev, &dname))
        return -1;

    if(!dname) {
        PyOS_snprintf(buf, sizeof(buf), "/dev/spidev%u.%u", bus, dev);
        dname = buf;
    }

    pvt->fd = open(dname, O_RDWR|O_CLOEXEC);
    if(pvt->fd<0) {
        PyErr_SetFromErrnoWithFilename(PyExc_OSError, dname);
        return -1;
    }

    return 0;
}

static
void spidev_dealloc(spidev *pvt)
{
    close(pvt->fd);
    Py_TYPE(pvt)->tp_free(pvt);
}

static
PyObject *spidev_xfer(spidev *pvt, PyObject *args, PyObject *kws)
{
    int err;
    const char *txbuf;
    PyObject *ret;
    char *rxbuf;
    Py_ssize_t buflen;
    unsigned int nbits=0;
    struct spi_ioc_transfer X[2];
    unsigned NX = 1;
    uint32_t mode = pvt->mode&3;

    char *names[] = {"data", "nbits", NULL};

    memset(X, 0, sizeof(X));

    if(!PyArg_ParseTupleAndKeywords(args, kws, "s#|I", names, &txbuf, &buflen, &nbits))
        return NULL;

    if(nbits>7)
        return PyErr_Format(PyExc_ValueError, "extrabits must be < 7 (not %u)", nbits);

    ret = PyString_FromStringAndSize(NULL, buflen);
    if(!ret)
        return NULL;

    if(buflen==0)
        return ret; /* a no-op */

    rxbuf = PyString_AS_STRING(ret);

    X[0].tx_buf = (unsigned long)txbuf;
    X[0].rx_buf = (unsigned long)rxbuf;
    X[0].len = buflen;
    X[0].bits_per_word = 8;
    X[0].speed_hz      = X[1].speed_hz      = pvt->speed;
    X[0].delay_usecs   = X[1].delay_usecs   = 10;

    if(buflen==1) {
        X[0].bits_per_word = nbits;

    } else if(nbits>0) {
        /* add a second transfer of the trailing bits */
        NX = 2;
        X[0].len-= 1;
        X[1].len = 1;
        X[1].bits_per_word = nbits;

        X[1].tx_buf = X[0].tx_buf + X[0].len;
        X[1].rx_buf = X[0].rx_buf + X[0].len;
    }

    err = ioctl(pvt->fd, SPI_IOC_WR_MODE32, &mode);
    if(err==-1) {
        PyErr_SetFromErrnoWithFilename(PyExc_OSError, "mode");
        return NULL;
    }

    err = ioctl(pvt->fd, SPI_IOC_MESSAGE(NX), X);
    if(err==-1) {
        PyErr_SetFromErrnoWithFilename(PyExc_OSError, "transfer");
        return NULL;
    }

    return ret;
}

static
struct PyMemberDef spidev_members[] = {
    {"mode", T_UINT, offsetof(spidev, mode), 0, "SPI mode 0-3"},
    {"speed", T_UINT, offsetof(spidev, speed), 0, "Bit rate in Hz"},
    {NULL}
};

static
struct PyMethodDef spidev_methods[] = {
    {"xfer", (PyCFunction)&spidev_xfer, METH_VARARGS,
     "Perform transfer"},
    {NULL}
};

static PyTypeObject spidev_type = {
#if PY_MAJOR_VERSION >= 3
    PyVarObject_HEAD_INIT(NULL, 0)
#else
    PyObject_HEAD_INIT(NULL)
    0,
#endif
    "gpiomem._lspi.SPI",
    sizeof(spidev),
};

#if PY_MAJOR_VERSION >= 3
static struct PyModuleDef spidevmodule = {
  PyModuleDef_HEAD_INIT,
    "gpiomem._lspi",
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
PyMODINIT_FUNC PyInit__lspi(void)
#else
PyMODINIT_FUNC init_lspi(void)
#endif
{
    PyObject *mod = NULL;

#if PY_MAJOR_VERSION >= 3
    mod = PyModule_Create(&spidevmodule);
#else
    mod = Py_InitModule("gpiomem._lspi", NULL);
#endif

    spidev_type.tp_flags = Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE;
    spidev_type.tp_members = spidev_members;
    spidev_type.tp_methods = spidev_methods;
    spidev_type.tp_init = (initproc)spidev_init;
    spidev_type.tp_dealloc = (destructor)spidev_dealloc;

    spidev_type.tp_new = PyType_GenericNew;
    if(PyType_Ready(&spidev_type)<0) {
        fprintf(stderr, "spidev object not ready\n");
        MODINIT_RET(NULL);
    }

    PyObject *typeobj=(PyObject*)&spidev_type;
    Py_INCREF(typeobj);
    if(PyModule_AddObject(mod, "SPI", typeobj)) {
        Py_DECREF(typeobj);
        fprintf(stderr, "Failed to add GPIOMEM object to module\n");
        MODINIT_RET(NULL);
    }

    MODINIT_RET(mod);
}
