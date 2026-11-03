/* Copyright (c) 2013-2018 GomSpace A/S. All rights reserved. */
#include <Python.h>

#include <gs/csp/drivers/i2c/i2c.h>

#if PY_MAJOR_VERSION == 3
#define IS_PY3
#endif

/* gs_error_t gs_csp_i2c_init(uint8_t device, uint8_t csp_addr); */
static PyObject* pygscsp_csp_i2c_init(PyObject *self, PyObject *args) {
    uint8_t device;
    uint8_t csp_addr;

    if (!PyArg_ParseTuple(args, "BB", &device, &csp_addr)) {
        Py_RETURN_NONE;
    }

    return Py_BuildValue("i", gs_csp_i2c_init(device, csp_addr));
}


static PyMethodDef methods[] = {

    {"i2c_init", pygscsp_csp_i2c_init, METH_VARARGS, ""},

    /* sentinel */
    {NULL, NULL, 0, NULL}
};

#ifdef IS_PY3
static struct PyModuleDef moduledef = {
    PyModuleDef_HEAD_INIT,
    "libgscsp_py3",
    NULL,
    -1,
    methods,
    NULL,
    NULL,
    NULL,
    NULL
};
#endif

#ifdef IS_PY3
PyMODINIT_FUNC PyInit_libgscsp_py3(void) {
#else
PyMODINIT_FUNC initlibgscsp_py2(void) {
#endif

#ifdef IS_PY3
        PyObject* m = PyModule_Create(&moduledef);
#else
        Py_InitModule("libgscsp_py2", methods);
#endif

#ifdef IS_PY3
        return m;
#endif
    }

