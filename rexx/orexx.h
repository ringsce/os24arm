/* ============================================================================
 * rexx/orexx.h  —  Object REXX Extensions
 *
 * Object-oriented extensions for REXX including classes, methods, and
 * message passing.
 * ========================================================================== */

#ifndef OREXX_H
#define OREXX_H

#include "types.h"

/* ── Object Model ────────────────────────────────────────────────────────── */

typedef struct ORexxObject   ORexxObject;
typedef struct ORexxClass    ORexxClass;
typedef struct ORexxMethod   ORexxMethod;
typedef struct ORexxMessage  ORexxMessage;

/* Object structure - base for all Object REXX objects */
struct ORexxObject {
    ORexxClass  *class;           /* Metaclass pointer */
    void        *instance_vars;   /* Instance variable storage */
    uint32_t     ref_count;       /* Reference counting for GC */
    uint32_t     flags;           /* Object flags */
};

/* Class structure - defines object behavior */
struct ORexxClass {
    ORexxObject  base;            /* Inherit from Object */
    char        *name;            /* Class name */
    ORexxClass  *superclass;      /* Parent class (NULL = Object) */
    ORexxMethod *methods;         /* Method dictionary */
    uint32_t     method_count;    /* Number of methods */
    uint32_t     instance_size;   /* Size of instance data */
};

/* Method structure - executable behavior */
struct ORexxMethod {
    char        *name;            /* Method name */
    void        *code;            /* Method implementation (AST or native) */
    uint32_t     flags;           /* PUBLIC, PRIVATE, PROTECTED */
    ORexxClass  *owner;           /* Class that owns this method */
};

/* Message structure - for method invocation */
struct ORexxMessage {
    ORexxObject *receiver;        /* Target object */
    char        *selector;        /* Method name */
    ORexxObject **args;           /* Argument array */
    uint32_t     arg_count;       /* Number of arguments */
};

/* ── Object Flags ────────────────────────────────────────────────────────── */

#define OREXX_FLAG_IMMUTABLE  0x0001  /* Cannot modify object */
#define OREXX_FLAG_FINALIZING 0x0002  /* Object being destroyed */

/* ── Method Visibility ───────────────────────────────────────────────────── */

#define OREXX_METHOD_PUBLIC    0x0001
#define OREXX_METHOD_PRIVATE   0x0002
#define OREXX_METHOD_PROTECTED 0x0004
#define OREXX_METHOD_ATTRIBUTE 0x0008  /* Auto-generated getter/setter */

/* ── Core Object System API ──────────────────────────────────────────────── */

/* Object lifecycle */
ORexxObject* orexx_object_new(ORexxClass *class);
void         orexx_object_init(ORexxObject *obj, ORexxClass *class);
void         orexx_object_free(ORexxObject *obj);
void         orexx_object_retain(ORexxObject *obj);
void         orexx_object_release(ORexxObject *obj);

/* Class management */
ORexxClass*  orexx_class_new(const char *name, ORexxClass *superclass);
void         orexx_class_add_method(ORexxClass *class, const char *name,
                                    void *code, uint32_t flags);
ORexxMethod* orexx_class_lookup_method(ORexxClass *class, const char *name);

/* Message sending */
ORexxObject* orexx_send_message(ORexxObject *receiver, const char *selector,
                                ORexxObject **args, uint32_t arg_count);

/* Instance variables */
void         orexx_set_instance_var(ORexxObject *obj, const char *name,
                                   ORexxObject *value);
ORexxObject* orexx_get_instance_var(ORexxObject *obj, const char *name);

/* Type checking */
int          orexx_is_instance_of(ORexxObject *obj, ORexxClass *class);
int          orexx_is_kind_of(ORexxObject *obj, ORexxClass *class);

/* ── Built-in Classes ────────────────────────────────────────────────────── */

extern ORexxClass *orexx_class_Object;
extern ORexxClass *orexx_class_Class;
extern ORexxClass *orexx_class_String;
extern ORexxClass *orexx_class_Array;
extern ORexxClass *orexx_class_Directory;
extern ORexxClass *orexx_class_Method;

/* ── Object REXX Runtime ─────────────────────────────────────────────────── */

void orexx_init(void);           /* Initialize Object REXX subsystem */
void orexx_shutdown(void);       /* Cleanup Object REXX subsystem */

#endif /* OREXX_H */