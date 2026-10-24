// Drafts for HeroPathfinderQueue: functions that do not match yet. NOT listed in
// unit.json "sources". Compile with: cl /nologo /c /O2 /G7 /GR /W3 drafts.cpp
//
// 0x00c08f30 Array2D<Node>::Alloc (see attempts.txt)
//
// Best draft: 57 of 59 instructions identical to the exe (same size, 149 bytes). The
// loop body zeroes the node fields in the right places and the induction register is
// the right one (ecx = node + 0x38), and the mask 0x7ffffff sits in edi as in the exe.
// The only difference is the order of the first two stores of the loop body:
//     exe:    and [node+4], edi   ; then   mov byte [node+3], bl   ...
//     draft:  mov byte [node+3], bl   ; then   and [node+4], edi   ...
// i.e. the clearing of the flags' top 5 bits comes before the clearing of the
// position's top byte in the exe, but after it in this draft.
//
// What was learned (compiler behaviour, VC++ 2003 13.10.3077 /O2 /G7):
//  - Writing the constructor as plain statements gives the exe's "and with edi" and
//    hoists that AND to the front, but then the induction register is the address of
//    the SECOND statement's field, never node+0x38 (exhaustive-ish search over the
//    orders of the 8 stores found at best 55 of 59 instructions).
//  - Writing the fields as sub-objects with their own constructors (as below) gives
//    the exe's induction register (node+0x38) and the "and with edi", but the stores
//    then come in member order (position, flags, ...), without the hoisting of the AND.
//  - Searching every split of {pos, flags, prev, d18, cost, cost2} into "sub-object
//    constructor" and "constructor body statement" (1957 variants) found nothing better.

#include <new>

struct Pos {
	union {
		unsigned v;
		unsigned char b[4];
	};
	Pos() { b[3] = 0; }
};

struct Flags {
	unsigned v;
	Flags() { v &= 0x7ffffff; }
};

struct Cost {
	int a;
	unsigned pad;
	__int64 b;
	Cost()
	{
		a = 0;
		b = 0;
	}
};

struct Node {
	Pos pos;     // 0x00
	Flags flags; // 0x04
	int moved;   // 0x08
	unsigned tie;  // 0x0c
	unsigned d10;  // 0x10
	Pos prev;    // 0x14
	Pos d18;     // 0x18
	unsigned d1c;  // 0x1c
	Cost cost;   // 0x20
	Cost cost2;  // 0x30
};

struct Array2DNode {
	Node *data;
	Node **rows;
	int width;
	int height;

	void Alloc();
};

// FUNCTION: 0x00c08f30
void Array2DNode::Alloc()
{
	data = new Node[width * height];
	rows = new Node *[height];
	for (int i = 0; i < height; i++)
		rows[i] = data + width * i;
}
