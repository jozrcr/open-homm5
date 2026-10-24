// Drafts that do not match yet (see attempts.txt). Not listed in unit.json.
#include "SinglePlayerTransceiver.cpp"

// FUNCTION: 0x007c8490
// Closest version: same instructions as the exe's, but the exe loads the vector pointer
// and `last` and sets a zero register (xor ebx,ebx) before the `test al,al`, and stores
// the zeros from that register; this version tests first and uses ebx for &v->last.
void IBinSaver::AddVec(CmdVec *pv)
{
	int n;
	bool reading = IsReading();
	CmdVec *v = pv;
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
