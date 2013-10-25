#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <pin.H>
#include <map>
#include <set>
#include <utility>
#include <sstream>

#define MYREG_INVALID   ((REG)(REG_LAST + 1))
#define MYREG_JMPTARGET ((REG)(REG_LAST + 2))
struct insrecord {
	string opcode;
	REG reg;
	int count;
	int branch_taken; // -1 for instruction other than conditional branch
	ADDRINT low;
	ADDRINT high;
	bool iscall;
};

const char *logname = "instat.log";
const char *tsvname = "instat.tsv";
FILE *logfp;
std::map<ADDRINT,insrecord> insmap;
std::map<ADDRINT,string> symbols;
std::map<ADDRINT,std::pair<ADDRINT,string> > imgs;
std::set<ADDRINT> calltargets;

void img_load (IMG img, void *v)
{
	fprintf(logfp, "load %s off=%08x low=%08x high=%08x start=%08x size=%08x\n",
			IMG_Name(img).c_str(),
			IMG_LoadOffset(img), IMG_LowAddress(img), IMG_HighAddress(img),
			IMG_StartAddress(img), IMG_SizeMapped(img));

	string name = IMG_Name(img);
	size_t start = name.find_last_of("/\\");
	start = start == string::npos ? 0 : start + 1;
	size_t end = name.find_first_of('.', start);
	end = end == string::npos ? name.length() : end;
	if (end > start)
		name = name.substr(start, end - start);
	imgs[IMG_HighAddress(img)] = std::pair<ADDRINT,string>(IMG_LowAddress(img), name);

	for (SEC sec = IMG_SecHead(img); SEC_Valid(sec); sec = SEC_Next(sec)) {
		for(RTN rtn = SEC_RtnHead(sec); RTN_Valid(rtn); rtn = RTN_Next(rtn)) {
			fprintf(logfp, "%08x %s\n", RTN_Address(rtn), RTN_Name(rtn).c_str());
			symbols[RTN_Address(rtn)] = RTN_Name(rtn);
			calltargets.insert(RTN_Address(rtn));
		}
	}

/*	for (SYM sym = IMG_RegsymHead(img); SYM_Valid(sym); sym = SYM_Next(sym)) {
		ADDRINT addr = IMG_StartAddress(img) + SYM_Value(sym);
		fprintf(logfp, "%08x %s\n", addr, SYM_Name(sym).c_str());
		symbols[addr] = SYM_Name(sym);
		calltargets.insert(addr);
	}*/
}

void on_branch_taken (struct insrecord *rec)
{
	rec->branch_taken ++;
}

void on_ins (struct insrecord *rec, ADDRINT regval, BOOL isindcall)
{
	rec->low = min(rec->low, regval);
	rec->high = max(rec->high, regval);
	rec->count ++;
	if (isindcall)
		calltargets.insert(regval);
}

inline bool REG_is_integer (REG reg) {
	return (reg >= REG_RBASE && reg < REG_MM_BASE);
}

void instruction (INS ins, void *v)
{
	ADDRINT addr = INS_Address(ins);

	if (insmap.find(addr) != insmap.end())
		return;

	struct insrecord record;
	record.opcode = INS_Disassemble(ins);
	record.count = 0;
	record.branch_taken = -1;
	record.low = -1;
	record.high = 0;
	record.iscall = INS_IsCall(ins);

	if (INS_IsBranchOrCall(ins)) {
		record.reg = MYREG_JMPTARGET;
		if (INS_IsDirectCall(ins)) {
			calltargets.insert(INS_DirectBranchOrCallTargetAddress(ins));
			record.low = record.high = INS_DirectBranchOrCallTargetAddress(ins);
		}
	} else {
		record.reg = MYREG_INVALID;
		for (UINT32 regindex = 0; regindex < INS_OperandCount(ins); regindex ++) {
			if (INS_OperandRead(ins, regindex) && INS_OperandIsReg(ins, regindex) && REG_is_integer(INS_OperandReg(ins, regindex))) {
				record.reg = INS_OperandReg(ins, regindex);
				break;
			}
		}
		if (record.reg == MYREG_INVALID && INS_MaxNumRRegs(ins) > 0 && REG_is_integer(INS_RegR(ins, 0)))
			record.reg = INS_RegR(ins, 0);
	}

	insmap[addr] = record;

	if (record.reg == MYREG_JMPTARGET) {
		INS_InsertCall(ins, IPOINT_BEFORE, AFUNPTR(on_ins),
				IARG_ADDRINT, &insmap[addr], // we assume std::map doesn't move our data.
				IARG_BRANCH_TARGET_ADDR,
				IARG_BOOL, INS_IsCall(ins) && (!INS_IsDirectCall(ins)),
				IARG_END);
	} else {
		INS_InsertCall(ins, IPOINT_BEFORE, AFUNPTR(on_ins),
				IARG_ADDRINT, &insmap[addr],
				record.reg == MYREG_INVALID ? IARG_ADDRINT : IARG_REG_VALUE,
				record.reg == MYREG_INVALID ? 0 : record.reg,
				IARG_BOOL, false,
				IARG_END);
	}
	if (INS_IsBranch(ins) && INS_HasFallThrough(ins)) {
		insmap[addr].branch_taken = 0;
		INS_InsertCall(ins, IPOINT_TAKEN_BRANCH, AFUNPTR(on_branch_taken),
				IARG_ADDRINT, &insmap[addr],
				IARG_END);
	}
}

string get_rtn_name (ADDRINT addr, bool full)
{
	std::map<ADDRINT,std::pair<ADDRINT,string> >::iterator it = imgs.upper_bound(addr);
	std::stringstream ss;
	if (it->first == 0 || addr < it->second.first)
		ss << "[?].";
	else
		ss << "[" << it->second.second << "].";

	if (symbols.find(addr) == symbols.end())
		ss << std::hex << addr;
	else
		ss << PIN_UndecorateSymbolName(symbols[addr], full ? UNDECORATION_COMPLETE : UNDECORATION_NAME_ONLY);
	return ss.str();
}

void on_fini (INT32 code, void *v)
{
	fprintf(logfp, "fini %d\n", code);
	FILE *fp = fopen(tsvname, "w");
	for(std::map<ADDRINT,insrecord>::iterator ite = insmap.begin(); ite != insmap.end(); ite ++) {
		fprintf(fp, "%x\t%s\t%d\t%s",
				ite->first, ite->second.opcode.c_str(), ite->second.count,
				ite->second.reg == MYREG_INVALID ? "-" :
				(ite->second.reg == MYREG_JMPTARGET ? "->" : REG_StringShort(ite->second.reg).c_str()));

		if (ite->second.count == 0 || ite->second.reg == MYREG_INVALID)
			fprintf(fp, "\t-\t-");
		else
			fprintf(fp, "\t%x\t%x", ite->second.low, ite->second.high);

		if (calltargets.find(ite->first) != calltargets.end())
			fprintf(fp, "\tentry: %s", get_rtn_name(ite->first, true).c_str());

		if (ite->second.branch_taken != -1) {
			fprintf(fp, "\tbrtaken: %d", ite->second.branch_taken);
		}

		if (ite->second.reg == MYREG_JMPTARGET && ite->second.iscall &&
				(ite->second.count != 0 || ite->second.low != 0)) {
			if (ite->second.low == ite->second.high) {
				fprintf(fp, "\ttarget: %s", get_rtn_name(ite->second.low, false).c_str());
			} else {
				fprintf(fp, "\ttarget: %s - %s",
						get_rtn_name(ite->second.low, false).c_str(),
						get_rtn_name(ite->second.high, false).c_str());
			}
		}
		fprintf(fp, "\n");
	}
	fclose(fp);
	fclose(logfp);
}

int main (int argc, char *argv[])
{
	if(PIN_Init(argc, argv)) {
		fprintf(stderr, "command line error\n");
		return 1;
	}

	logfp = fopen(logname, "w");

	PIN_InitSymbols();

	PIN_AddFiniFunction(on_fini, 0);
	IMG_AddInstrumentFunction(img_load, NULL);
	INS_AddInstrumentFunction(instruction, NULL);

	imgs[0] = std::pair<ADDRINT,string>(1, "dummy");
	PIN_StartProgram(); // Never returns
	return 0;
}
