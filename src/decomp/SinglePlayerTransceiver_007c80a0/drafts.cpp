// Drafts that do not match yet (see attempts.txt). Not listed in unit.json.
#include "SinglePlayerTransceiver.cpp"

// FUNCTION: 0x007c8490
// Closest version (201 bytes, the exe's length; the zero register, the hoisted load of
// `last` and the stores from the zero register all come out like the exe's). What still
// differs: the exe loads the vector pointer AFTER the IsReading() call and tests with
// `test al,al`; this version loads it before the call and tests with `cmp al,bl`.
// Moving `v = pv` after the call loses the zero register (ebx then holds &v->last).
void IBinSaver::AddVec(CmdVec *pv)
{
	int n;
	CmdVec *v = pv;
	bool reading = IsReading();
	CCmd **last = v->last;
	if (reading) {
		DestroyRange(v->first, last);
		if (v->first)
			Free(v->first);
		v->first = 0;
		v->last = 0;
		v->end = 0;
		if (GetVersion() < 2) {
			n = CountChunks(1);
			v->Resize(n);
		} else {
			DataChunk(2, &n, 4, 1);
			v->Resize(n);
		}
	} else {
		n = last - v->first;
		DataChunk(2, &n, 4, 1);
	}
	for (int i = 0; i < n; ++i)
		Chunk(this, 1, i + 1, &v->first[i], &pv);
}
