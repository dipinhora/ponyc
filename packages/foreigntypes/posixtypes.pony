// This file defines the standard Posix type to make FFI calls easier
// to manage and more consistent.

type BlkCnt is I64

// not consistent across OSX (always 32 bit int)/Linux (Long)
type BlkSize is ILong
type OSXBlkSize is I32

type Cc is U8

// not consistent across OSX (Unsigned Long)/Linux (Long)
type Clock is ILong
type OSXClock is ULong

// not defined on OSX
type ClockId is I32

// not consistent across OSX (always 32 bit int)/Linux (always 64 bit)
type Dev is I64
type OSXDev is I32

// not consistent across OSX (always unsigned int)/Linux (always unsigned long)
type FsBlkCnt is ULong
type OSXFsBlkCnt is U32

// not consistent across OSX (always unsigned int)/Linux (always unsigned long)
type FsFilCnt is ULong
type OSXFsFilCnt is U32

type Gid is U32

type Id is U32

type INo is U64

type Key is I32

// not consistent across OSX (always unsigned 16 bit int)/Linux (always unsigned int)
type Mode is ULong
type OSXMode is U16

type Mqd is I32

// not consistent across OSX (always unsigned int)/Linux (always unsigned Long)
type Nfds is ULong
type OSXNfds is U32

// not consistent across OSX (always unsigned 16 bit int)/Linux (unsigned Long)
type NLink is ULong
type OSXNLink is U16

type Off is I64

type Pid is I32

// not defined on linux
type PtrDiff is ISize

type RLim is I64

type SigAtomic is I32

type Size is USize

// not consistent across OSX (always unsigned long)/Linux (always unsigned int)
type Speed is ULong

type SSize is ISize

// not consistent across OSX (always int)/Linux (always long)
type SuSeconds is ILong

// not consistent across OSX (always unsigned long)/Linux (always unsigned int)
type TcFlag is U32
type OSXTcFlag is ULong

type Time is ILong

// not defined on OSX/void* on Linux
type Timer is Pointer[None]

type Uid is U32

type USeconds is U32

type Wchar is I32

// not consistent across OSX (always unsigned 32 bit)/Linux (always unsigned long)
type WCType is ULong
type OSXWCType is U32

// not consistent across OSX (always signed 32 bit)/Linux (always unsigned 32 bit)
type WInt is U32
type OSXWInt is I32
