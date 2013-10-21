#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <pin.H>
#include <map>

struct insrecord {
	string opcode;
	int count;
};

const char *imgname = "ld-linux.so.2";
ADDRINT imglow = 0;
ADDRINT imghigh = 0;
bool imgloaded = false;
std::map<ADDRINT,insrecord> insmap;

void img_load (IMG img, void *v)
{
	fprintf(stderr, "load %s off=%08x low=%08x high=%08x start=%08x size=%08x\n",
			IMG_Name(img).c_str(),
			IMG_LoadOffset(img), IMG_LowAddress(img), IMG_HighAddress(img),
			IMG_StartAddress(img), IMG_SizeMapped(img));
	if (IMG_Name(img).rfind(imgname) != std::string::npos) {
		fprintf(stderr, "matched\n");
		imglow = IMG_LowAddress(img);
		imghigh = IMG_HighAddress(img);
		imgloaded = true;
	}
}

void img_unload (IMG img, void *v)
{
	if (IMG_Name(img).rfind(imgname) != std::string::npos) {
		fprintf(stderr, "unload matched\n");
		imgloaded = false;
	}
}

void on_ins (ADDRINT insaddr)
{
	insmap[insaddr].count ++;
}

void instruction (INS ins, void *v)
{
	ADDRINT addr = INS_Address(ins);
	//fprintf(stderr, "%x\n", addr);

	if (imgloaded && addr >= imglow && addr <= imghigh) {
		//fprintf(stderr, "%x\n", addr);
		if (insmap.find(addr) == insmap.end()) {
			struct insrecord record;
			record.opcode = INS_Mnemonic(ins);
			record.count = 0;
			//insmap.insert(addr, record);
			insmap[addr] = record;
			INS_InsertCall(ins, IPOINT_BEFORE, AFUNPTR(on_ins),
					IARG_INST_PTR, IARG_END);
			//fprintf(stderr, "KKK %x\n", addr);
		}
	}
}

void on_fini (INT32 code, void *v)
{
	fprintf(stderr, "fini\n");
	for(std::map<ADDRINT,insrecord>::iterator ite = insmap.begin(); ite != insmap.end(); ite ++) {
		printf("%x %s %d\n", ite->first, ite->second.opcode.c_str(), ite->second.count);
	}
}

int main (int argc, char *argv[])
{
	if(PIN_Init(argc, argv)) {
		printf("command line error\n");
		return 1;
	}

	PIN_InitSymbols();

	PIN_AddFiniFunction(on_fini, 0);
	IMG_AddInstrumentFunction(img_load, NULL);
	IMG_AddUnloadFunction(img_unload, NULL);
	INS_AddInstrumentFunction(instruction, NULL);

	PIN_StartProgram(); // Never returns
	return 0;
}
