/*
 * ***** BEGIN GPL LICENSE BLOCK *****
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) Blender Foundation
 * All rights reserved.
 *
 * The Original Code is: all of this file.
 *
 * Contributor(s): Lukas Toenne
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/blenvm/intern/bvm_nodegraph.h
 *  \ingroup bvm
 */

#ifndef __BVM_NODEGRAPH_H__
#define __BVM_NODEGRAPH_H__

#include <stdlib.h>
#include <string>
#include <iostream>
#include <map>
#include <vector>

extern "C" {
#include "BLI_utildefines.h"
#if 0
#include "DNA_node_types.h"
#include "BKE_node.h"
#endif
}

#include "bvm_opcode.h"
#include "bvm_util_string.h"
#include "bvm_util_typedesc.h"

namespace bvm {

struct NodeGraph;
struct NodeGraphInput;
struct NodeType;

struct NodeSocket {
	NodeSocket(const string &name, BVMType type, Value *default_value, bool constant);
	~NodeSocket();
	
	string name;
	BVMType type;
	Value *default_value;
	bool constant;
};

struct NodeType {
	typedef std::vector<NodeSocket> SocketList;
	
	NodeType(const string &name);
	~NodeType();
	
	const NodeSocket *find_input(int index) const;
	const NodeSocket *find_output(int index) const;
	const NodeSocket *find_input(const string &name) const;
	const NodeSocket *find_output(const string &name) const;
	/* stub implementation in case socket is passed directly */
	const NodeSocket *find_input(const NodeSocket *socket) const;
	const NodeSocket *find_output(const NodeSocket *socket) const;
	
//	bool verify_argument_socket(NodeSocket &socket, Type *type, int index,
//	                            Module *module, LLVMContext &context, raw_ostream &err);
//	bool verify_arguments(Module *module, LLVMContext &context, raw_ostream &err);
	
	const NodeSocket *add_input(const string &name, BVMType type, Value *default_value, bool constant=false);
	const NodeSocket *add_output(const string &name, BVMType type, Value *default_value);
	
	template <typename T>
	const NodeSocket *add_input(const string &name, BVMType type, T default_value, bool constant=false)
	{
		Value *c = Value::create(type, default_value);
		BLI_assert(c != NULL);
		return add_input(name, type, c, constant);
	}
	
	template <typename T>
	const NodeSocket *add_output(const string &name, BVMType type, T default_value)
	{
		Value *c = Value::create(type, default_value);
		BLI_assert(c != NULL);
		return add_output(name, type, c);
	}
	
	string name;
	SocketList inputs;
	SocketList outputs;
};

struct NodeInstance {
	struct InputInstance {
		const NodeGraphInput *graph_input;
		NodeInstance *link_node;
		const NodeSocket *link_socket;
		Value *value;
	};
	
	struct OutputInstance {
		Value *value;
	};
	
	typedef std::map<string, InputInstance> InputMap;
	typedef std::pair<string, InputInstance> InputPair;
	typedef std::map<string, OutputInstance> OutputMap;
	typedef std::pair<string, OutputInstance> OutputPair;
	
	NodeInstance(const NodeType *type, const string &name);
	~NodeInstance();
	
	NodeInstance *find_input_link_node(const string &name) const;
	NodeInstance *find_input_link_node(int index) const;
	const NodeSocket *find_input_link_socket(const string &name) const;
	const NodeSocket *find_input_link_socket(int index) const;
	const NodeGraphInput *find_input_extern(const string &name) const;
	const NodeGraphInput *find_input_extern(int index) const;
	Value *find_input_value(const string &name) const;
	Value *find_input_value(int index) const;
	Value *find_output_value(const string &name) const;
	Value *find_output_value(int index) const;
	
	bool set_input_value(const string &name, Value *value);
	bool set_input_link(const string &name, NodeInstance *from_node, const NodeSocket *from_socket);
	bool set_input_extern(const string &name, const NodeGraphInput *graph_input);
	bool set_output_value(const string &name, Value *value);
	
	template <typename T>
	bool set_input_value(const string &name, const T &value)
	{
		const NodeSocket *socket = type->find_input(name);
		return socket ? set_input_value(name, Value::create(socket->type, value)) : false;
	}
	
	template <typename T>
	bool set_output_value(const string &name, const T &value)
	{
		const NodeSocket *socket = type->find_output(name);
		return socket ? set_output_value(name, Value::create(socket->type, value)) : false;
	}
	
	bool has_input_link(const string &name) const;
	bool has_input_link(int index) const;
	bool has_input_extern(const string &name) const;
	bool has_input_extern(int index) const;
	bool has_input_value(const string &name) const;
	bool has_input_value(int index) const;
	bool is_input_constant(const string &name) const;
	bool is_input_constant(int index) const;
	bool has_output_value(const string &name) const;
	bool has_output_value(int index) const;
	
	const NodeType *type;
	string name;
	InputMap inputs;
	OutputMap outputs;
};

struct NodeGraphInput {
	NodeGraphInput(const string &name, BVMType type) : name(name), type(type), value(NULL)
	{}
	string name;
	BVMType type;
	
	Value *value;
};

struct NodeGraphOutput {
	NodeGraphOutput(const string &name, BVMType type, Value *default_value) :
	    name(name), type(type), default_value(default_value), link_node(NULL), link_socket(NULL)
	{}
	string name;
	BVMType type;
	Value *default_value;
	
	NodeInstance *link_node;
	const NodeSocket *link_socket;
};

struct NodeGraph {
	typedef std::vector<NodeGraphInput> InputList;
	typedef std::vector<NodeGraphOutput> OutputList;
	
	typedef std::map<string, NodeType> NodeTypeMap;
	typedef std::pair<string, NodeType> NodeTypeMapPair;
	typedef std::map<string, NodeInstance> NodeInstanceMap;
	typedef std::pair<string, NodeInstance> NodeInstanceMapPair;
	
	
	static NodeTypeMap node_types;
	
	static const NodeType *find_node_type(const string &name);
	static NodeType *add_node_type(const string &name);
	static void remove_node_type(const string &name);
	
	NodeGraph();
	~NodeGraph();
	
	NodeInstance *get_node(const string &name);
	NodeInstance *add_node(const string &type, const string &name);
	
	template <typename FromT, typename ToT>
	bool add_link(NodeInstance *from_node, FromT from,
	              NodeInstance *to_node, ToT to)
	{
		if (!to_node || !from_node)
			return false;
		
		const NodeSocket *from_socket = from_node->type->find_output(from);
		const NodeSocket *to_socket = to_node->type->find_input(to);
		if (!from_socket || !to_socket)
			return false;
		
		to_node->set_input_link(to_socket->name, from_node, from_socket);
		
		return true;
	}
	
	template <typename FromT, typename ToT>
	bool add_link(const string &from_node, FromT from,
	              const string &to_node, ToT to)
	{
		return add_link(get_node(from_node), from, get_node(to_node), to);
	}
	
	const NodeGraphInput *get_input(int index) const;
	const NodeGraphOutput *get_output(int index) const;
	const NodeGraphInput *get_input(const string &name) const;
	const NodeGraphOutput *get_output(const string &name) const;
	const NodeGraphInput *add_input(const string &name, BVMType type);
	const NodeGraphOutput *add_output(const string &name, BVMType type, Value *default_value);
	void set_input_argument(const string &name, Value *value);
	void set_output_link(const string &name, NodeInstance *link_node, const string &link_socket);
	
	template <typename T>
	const NodeGraphOutput *add_output(const string &name, BVMType type, const T &default_value)
	{
		return add_output(name, type, Value::create(type, default_value));
	}
	
	void dump(std::ostream &stream = std::cout);
	
	NodeInstanceMap nodes;
	InputList inputs;
	OutputList outputs;
};

string get_node_type_from_opcode(OpCode op);
OpCode get_opcode_from_node_type(const string &node);
void register_opcode_node_types();

} /* namespace bvm */

#endif