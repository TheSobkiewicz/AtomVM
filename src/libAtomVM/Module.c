/***************************************************************************
 *   Copyright 2017 by Davide Bettio <davide@uninstall.it>                 *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU Lesser General Public License as        *
 *   published by the Free Software Foundation; either version 2 of the    *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA .        *
 ***************************************************************************/

#include "Module.h"

#include "Context.h"
#include "atom.h"
#include "bif.h"
#include "iff.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>

#ifdef WITH_ZLIB
#include <zlib.h>
#endif

#define IMPL_CODE_LOADER 1
#include "opcodesswitch.h"
#undef TRACE
#undef IMPL_CODE_LOADER

void module_build_imported_functions_table(Module *this_module, uint8_t *table_data, uint8_t *atom_tab)
{
    int functions_count = READ_32_ALIGNED(table_data + 8);

    this_module->imported_bifs = calloc(functions_count, sizeof(void *));

    for (int i = 0; i < functions_count; i++) {
        AtomString module_atom = local_atom_string(atom_tab, READ_32_ALIGNED(table_data + i * 12 + 12));
        AtomString function_atom = local_atom_string(atom_tab, READ_32_ALIGNED(table_data + i * 12 + 4 + 12));
        uint32_t arity = READ_32_ALIGNED(table_data + i * 12 + 8 + 12);

        if (bif_registry_is_bif(module_atom, function_atom, arity)) {
            this_module->imported_bifs[i] = bif_registry_get_handler(module_atom, function_atom, arity);
        } else {
            this_module->imported_bifs[i] = NULL;
        }
    }
}

uint32_t module_search_exported_function(Module *this_module, AtomString func_name, int func_arity)
{
    const uint8_t *table_data = (const uint8_t *) this_module->export_table;
    int functions_count = READ_32_ALIGNED(table_data + 8);

    for (int i = 0; i < functions_count; i++) {
        AtomString function_atom = local_atom_string(this_module->atom_table, READ_32_ALIGNED(table_data + i * 12 + 12));
        int32_t arity = READ_32_ALIGNED(table_data + i * 12 + 4 + 12);
        if ((func_arity == arity) && atom_are_equals(func_name, function_atom)) {
            uint32_t label = READ_32_ALIGNED(table_data + i * 12 + 8 + 12);
            return label;
        }
    }

    return 0;
}

void module_add_label(Module *mod, int index, void *ptr)
{
    mod->labels[index] = ptr;
}

Module *module_new_from_iff_binary(void *iff_binary, unsigned long size)
{
    uint8_t *beam_file = (void *) iff_binary;

    unsigned long offsets[MAX_OFFS];
    scan_iff(beam_file, size, offsets);

    Module *mod = malloc(sizeof(Module));

    module_build_imported_functions_table(mod, beam_file + offsets[IMPT], beam_file + offsets[AT8U]);

    mod->code = (CodeChunk *) (beam_file + offsets[CODE]);
    mod->export_table = beam_file + offsets[EXPT];
    mod->atom_table = beam_file + offsets[AT8U];
    mod->labels = calloc(ENDIAN_SWAP_32(mod->code->labels), sizeof(void *));

    read_core_chunk(mod);

    return mod;
}

void module_destroy(Module *module)
{
    free(module->labels);
    free(module->imported_bifs);
    free(module);
}

#ifdef WITH_ZLIB
static void *module_uncompress_literals(const uint8_t *litT, int size)
{
    unsigned int required_buf_size = READ_32_ALIGNED(litT + LITT_UNCOMPRESSED_SIZE_OFFSET);

    uint8_t *outBuf = malloc(required_buf_size);

    z_stream infstream;
    infstream.zalloc = Z_NULL;
    infstream.zfree = Z_NULL;
    infstream.opaque = Z_NULL;
    infstream.avail_in = (uInt) (size - IFF_SECTION_HEADER_SIZE);
    infstream.next_in = (Bytef *) (litT + LITT_HEADER_SIZE);
    infstream.avail_out = (uInt) required_buf_size;
    infstream.next_out = (Bytef *) outBuf;

    int ret = inflateInit(&infstream);
    if (ret != Z_OK) {
        fprintf(stderr, "Failed inflateInit\n");
        abort();
    }
    ret = inflate(&infstream, Z_NO_FLUSH);
    if (ret != Z_OK) {
        fprintf(stderr, "Failed inflate\n");
        abort();
    }
    inflateEnd(&infstream);

    return outBuf;
}
#endif
