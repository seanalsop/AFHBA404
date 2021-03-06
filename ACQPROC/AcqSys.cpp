/*
 * AcqSys.cpp
 *
 *  Created on: 27 Feb 2020
 *      Author: pgm
 */

#include "AcqSys.h"
#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include "nlohmann/json.hpp"
#include <string.h>

#include <stdio.h>		// because sprintf()

#include <assert.h>



using json = nlohmann::json;

VI::VI() {
	memset(this, 0, sizeof(VI));
}

int VI::len(void) const {
	return AI16*2 + AI32*4 + DI32*4 + SP32*4;
}
VI& VI::operator += (const VI& right) {
	this->AI16 += right.AI16;
	this->AI32 += right.AI32;
	this->DI32 += right.DI32;
	this->SP32 += right.SP32;
	return *this;
}

VI VI::offsets(void) const
{
	VI viff;
	viff.AI16 = 0;
	viff.AI32 = 0;
	assert(!(AI16 != 0 && AI32 != 0));
	assert(sizeof(int) == 4);
	viff.DI32 = AI16*sizeof(short) + AI32*sizeof(int);
	viff.SP32 = viff.DI32 + DI32*sizeof(int);
	return viff;
}

VO::VO() {
	memset(this, 0, sizeof(VO));
}
VO& VO::operator += (const VO& right) {
	this->AO16 += right.AO16;
	this->DO32 += right.DO32;
	this->CC32 += right.CC32;
	return *this;
}

VO VO::offsets(void) const
{
	VO voff;
	voff.AO16 = 0;
	voff.DO32 = AO16*sizeof(short);
	voff.CC32 = voff.DO32 + DO32*sizeof(unsigned);
	return voff;
}


int VO::len(void) const {
	return AO16*2 + DO32*4 + CC32*4;
}

int VO::hwlen(void) const {
	return AO16*2 + DO32*4;
}

IO::IO(string _name, VI _vi, VO _vo): name(_name), vi(_vi), vo(_vo), _string(0)
{

}

IO::~IO()
{
	if (_string) delete _string;
}

string IO::toString(void)
{
	return name + " VI:" + to_string(vi.len()) + " VO:" + to_string(vo.len());
}

void HBA::dump_config()
{
	cerr << toString() << endl;
	int port = 0;
	for (auto uut: uuts){
		cerr << "\t" << "[" << port << "] " << uut->toString() <<endl;
		++port;
	}
}

void HBA::dump_data(const char* basename)
{
	cerr << "dump_data" <<endl;
}


ACQ::ACQ(int devnum, string _name, VI _vi, VO _vo, VI _vi_offsets, VO _vo_offsets, VI& sys_vi_cursor, VO& sys_vo_cursor) :
		IO(_name, _vi, _vo),
		vi_offsets(_vi_offsets), vo_offsets(_vo_offsets),
		vi_cursor(sys_vi_cursor), vo_cursor(sys_vo_cursor),
		nowait(false), wd_mask(0), pollcount(0)
{
	// @@todo hook the device driver.
	sys_vi_cursor += vi;
	sys_vo_cursor += vo;
}

ACQ::~ACQ() {

}
string ACQ::toString() {
	char wd[80] = {};
	if (wd_mask){
		sprintf(wd, " WD mask: 0x%08x", wd_mask);
	}
	return IO::toString() + " Offset of SPAD IN VI :" + to_string(vi_offsets.SP32) + "\n"
			" System Interface Indices " + to_string(vi_cursor.AI16)+ "," + to_string(vi_cursor.SP32) + wd;
}
bool ACQ::newSample(int sample)
{
	cerr << " new sample: " << sample << " " << getName() << endl;
	return true;
}

unsigned ACQ::tlatch(void)
{
	return 0;
}

void ACQ::arm(int nsamples)
{
	cerr << "placeholder: ARM unit " << getName() << " now" <<endl;
}

int get_int(json j)
{
	try {
		return	j.get<int>();
	} catch (std::exception& e){
		return 0;
	}
}

HBA::HBA(int _devnum, vector <ACQ*> _uuts, VI _vi, VO _vo):
		IO("HBA"+to_string(_devnum), _vi, _vo),
		uuts(_uuts), vi(_vi), vo(_vo)
{

}

HBA::~HBA() {
    for (auto uut : uuts){
        delete uut;
    }
}
extern "C" {
	int sched_fifo_priority;
}


void HBA::processSample(SystemInterface& systemInterface, int sample)
{
	for (auto uut : uuts){
		while(!uut->newSample(sample)){
			sched_fifo_priority>1 || sched_yield();
			++uut->pollcount;
		}
	}
	for (auto uut : uuts){
		uut->action(systemInterface);
	}
	systemInterface.ringDoorbell(sample);
	for (auto uut : uuts){
		uut->action2(systemInterface);
	}
}


typedef pair<std::string, int> KVP;		/** Key Value Pair */
typedef std::map <std::string, int> KVM;	/** Key Value Map  */

typedef pair<std::string, string> KVPS; /** Key Value Pair, COMMENT */
typedef std::map <std::string, string> KVMS;	/** Key Value Map  */

//#define COM(comment) KVPS("__comment__", comment)

#define COM(i) "__comment" #i "__"

#define INSERT_IF(map, vx, vxo, field) \
	if (uut->vx.field){	\
		map.insert(KVP(#field, uut->vxo.field)); \
	}

void add_comments(json& jsys, string& fname)
{
	jsys[COM(1)] = "created from " + fname;
	jsys[COM(2)] = "LOCAL VI_OFFSETS: field offset VI in bytes";
	jsys[COM(3)] = "LOCAL VO_OFFSETS: field offset VO in bytes";
	jsys[COM(4)] = "LOCAL VX_LEN: length of VI|VO in bytes";
	jsys[COM(4)] = "GLOBAL_INDICES: index of field in type-specific array in SI";
	jsys[COM(5)] = "SPIX: Scratch Pad Index, index of field in SP32";


	KVM spix_map;
	spix_map.insert(KVP("TLATCH", 	 SPIX::TLATCH));
	spix_map.insert(KVP("USECS", 	 SPIX::USECS));
	spix_map.insert(KVP("POLLCOUNT", SPIX::POLLCOUNT));
	jsys["SPIX"] = spix_map;
}

void store_config(json j, string fname, HBA& hba)
{
	json &jsys = j["SYS"];
	add_comments(jsys, fname);

	int ii = 0;
	for (auto uut : hba.uuts){
		json &jlo = jsys["UUT"]["LOCAL"][ii];

		jlo["VX_LEN"] = { {  "VI", uut->vi.len() }, { "VO", uut->vo.len() } };

		KVM vi_offsets_map;
		INSERT_IF(vi_offsets_map, vi, vi_offsets, AI16);
		INSERT_IF(vi_offsets_map, vi, vi_offsets, AI32);
		INSERT_IF(vi_offsets_map, vi, vi_offsets, DI32);
		INSERT_IF(vi_offsets_map, vi, vi_offsets, SP32);
		jlo["VI_OFFSETS"] = vi_offsets_map;

		KVM vo_offsets_map;
		INSERT_IF(vo_offsets_map, vo, vo_offsets, AO16);
		INSERT_IF(vo_offsets_map, vo, vo_offsets, DO32);
		INSERT_IF(vo_offsets_map, vo, vo_offsets, CC32);
		jlo["VO_OFFSETS"] = vo_offsets_map;


		json &jix = jsys["UUT"]["GLOBAL_INDICES"][ii++];
		KVM vi_map;
		INSERT_IF(vi_map, vi, vi_cursor, AI16);
		INSERT_IF(vi_map, vi, vi_cursor, AI32);
		INSERT_IF(vi_map, vi, vi_cursor, DI32);
		INSERT_IF(vi_map, vi, vi_cursor, SP32);
		jix["VI"] = vi_map;

		KVM vo_map;
		INSERT_IF(vo_map, vo, vo_cursor, AO16);
		INSERT_IF(vo_map, vo, vo_cursor, DO32);
		INSERT_IF(vo_map, vo, vo_cursor, CC32);
		jix["VO"] = vo_map;
	}


	std::ofstream o("runtime.json");
	o << std::setw(4) << j << std::endl;
}

int HBA::maxsam;

HBA* HBA::the_hba;

/** HBA::Create() factory function. */
HBA& HBA::create(const char* json_def, int _maxsam)
{
	json j;
	std::ifstream i(json_def);
	i >> j;

	maxsam = _maxsam;
	struct VI VI_sys;
	struct VO VO_sys;
	vector <ACQ*> uuts;
	int port = 0;
	string port0_type;

	int hba_devnum = get_int(j["AFHBA"]["DEVNUM"]);

	bool HW = getenv("HW") != 0 && atoi(getenv("HW"));

	for (auto uut : j["AFHBA"]["UUT"]) {

		VI vi;

		vi.AI16 = get_int(uut["VI"]["AI16"]);
		vi.AI32 = get_int(uut["VI"]["AI32"]);
		vi.DI32 = get_int(uut["VI"]["DI32"]);
		vi.SP32 = get_int(uut["VI"]["SP32"]);

		VO vo;
		vo.AO16 = get_int(uut["VO"]["AO16"]);
		vo.DO32 = get_int(uut["VO"]["DO32"]);
		vo.CC32 = get_int(uut["VO"]["CC32"]);

		ACQ *acq = HW?
				new ACQ_HW(hba_devnum+port, uut["name"], vi, vo, vi.offsets(), vo.offsets(), VI_sys, VO_sys) :
				new    ACQ(hba_devnum+port, uut["name"], vi, vo, vi.offsets(), vo.offsets(), VI_sys, VO_sys);

		try {
			int wd_bit = uut["WD_BIT"].get<int>();
			if (port == 0){
				acq->wd_mask = 1 << wd_bit;
			}else{
				cerr << "WARNING: " << uut["name"] << " attempted to set WD_BIT when PORT (" << port << ") != 0" <<endl;
			}
		} catch (exception& e) {
			;
		}
		// check unit compatibility
		if (port == 0){
			port0_type = uut["type"];
		}else{
			if (port0_type == "pcs" && uut["type"] == "bolo"){
				cerr << "NOTICE: port " << port << " is bolo in non-bolo set, set nowait" << endl;
				acq->nowait = true;
			}else if (port0_type == "bolo" && uut["type"] != "bolo"){
				cerr << "WARNING: port " << port << " is NOT bolo when port0 IS bolo" << endl;
			}
		}
		uuts.push_back(acq);
		++port;
	}
	the_hba = new HBA(hba_devnum, uuts, VI_sys, VO_sys);
	store_config(j, json_def, *the_hba);
	return *the_hba;
}



