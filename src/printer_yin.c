/**
 * @file printer_yin.c
 * @author Michal Vasko <mvasko@cesnet.cz>
 * @brief YIN printer for libyang data model structure
 *
 * Copyright (c) 2016 CESNET, z.s.p.o.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of the Company nor the names of its contributors
 *    may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 */

#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "printer.h"
#include "tree_schema.h"
#include "xml_internal.h"

#define INDENT ""
#define LEVEL (level*2)

static void yin_print_snode(struct lyout *out, int level, const struct lys_node *node, int mask);

static void
yin_print_open(struct lyout *out, const char *elem_name, const char *attr_name, const char *attr_value, int level,
               int close)
{
    ly_print(out, "%*s<%s %s=\"%s\"%s>\n", LEVEL, INDENT, elem_name, attr_name, attr_value, (close ? "/" : ""));
}

static void
yin_print_close(struct lyout *out, const char *elem_name, int level)
{
    ly_print(out, "%*s</%s>\n", LEVEL, INDENT, elem_name);
}

static void
yin_print_unsigned(struct lyout *out, const char *elem_name, const char *attr_name, unsigned int attr_value, int level)
{
    ly_print(out, "%*s<%s %s=\"%u\"/>\n", LEVEL, INDENT, elem_name, attr_name, attr_value);
}

static void
yin_print_text(struct lyout *out, int level, const char *elem_name, const char *text)
{
    ly_print(out, "%*s<%s>\n", LEVEL, INDENT, elem_name);

    level++;
    ly_print(out, "%*s<text>", LEVEL, INDENT);
    lyxml_dump_text(out, text);
    ly_print(out, "</text>\n");
    level--;

    ly_print(out, "%*s</%s>\n", LEVEL, INDENT, elem_name);
}

static void
yin_print_restr_sub(struct lyout *out, const struct lys_restr *restr, int level)
{
    if (restr->dsc) {
        yin_print_text(out, level, "description", restr->dsc);
    }
    if (restr->ref) {
        yin_print_text(out, level, "reference", restr->ref);
    }
    if (restr->eapptag) {
        yin_print_open(out, "error-app-tag", "value", restr->eapptag, level, 1);
    }
    if (restr->emsg) {
        ly_print(out, "%*s<error-message>\n", LEVEL, INDENT);

        level++;
        ly_print(out, "%*s<value>", LEVEL, INDENT, restr->emsg);
        lyxml_dump_text(out, restr->emsg);
        ly_print(out, "</value>\n");
        level--;

        yin_print_close(out, "error-message", level);
    }
}

static void
yin_print_restr(struct lyout *out, const char *elem_name, const struct lys_restr *restr, int level)
{
    int close;

    close = (restr->dsc || restr->ref || restr->eapptag || restr->emsg ? 0 : 1);

    yin_print_open(out, elem_name, "value", restr->expr, level, close);
    if (!close) {
        yin_print_restr_sub(out, restr, level);

        yin_print_close(out, elem_name, level);
    }
}

static int
yin_has_nacmext(const struct lys_node *node)
{
    if (node->nacm && (!node->parent || node->parent->nacm != node->nacm)) {
        return 1;
    }
    return 0;
}

static void
yin_print_nacmext(struct lyout *out, int level, const struct lys_node *node, const struct lys_module *module)
{
    int i, j;
    const char *prefix = NULL;

    if (node->nacm && (!node->parent || node->parent->nacm != node->nacm)) {
        /* locate ietf-netconf-acm module in imports */
        if (!strcmp(module->name, "ietf-netconf-acm")) {
            prefix = module->prefix;
        } else {
            /* search in imports */
            for (i = 0; i < module->imp_size; i++) {
                if (!strcmp(module->imp[i].module->name, "ietf-netconf-acm")) {
                    prefix = module->imp[i].prefix;
                    break;
                }
            }
            /* and in imports of includes */
            if (!prefix) {
                for (j = 0; j < module->inc_size; j++) {
                    for (i = 0; i < module->inc[j].submodule->imp_size; i++) {
                        if (!strcmp(module->inc[j].submodule->imp[i].module->name, "ietf-netconf-acm")) {
                            prefix = module->inc[j].submodule->imp[i].prefix;
                            break;
                        }
                    }
                }
            }
        }

        if ((node->nacm & LYS_NACM_DENYW) && (!node->parent || !(node->parent->nacm & LYS_NACM_DENYW))) {
            ly_print(out, "%*s<%s:default-deny-write/>\n", LEVEL, INDENT, prefix);
        }
        if ((node->nacm & LYS_NACM_DENYA) && (!node->parent || !(node->parent->nacm & LYS_NACM_DENYA))) {
            ly_print(out, "%*s<%s:default-deny-all/>\n", LEVEL, INDENT, prefix);
        }
    }
}

static int
yin_has_snode_common(const struct lys_node *node)
{
    if ((node->flags & LYS_STATUS_MASK) || node->dsc || node->ref) {
        return 1;
    }
    return 0;
}

/*
 * Covers:
 * description, reference, status
 */
static void
yin_print_snode_common(struct lyout *out, int level, const struct lys_node *node)
{
    if (node->flags & LYS_STATUS_CURR) {
        yin_print_open(out, "status", "value", "current", level, 1);
    } else if (node->flags & LYS_STATUS_DEPRC) {
        yin_print_open(out, "status", "value", "deprecated", level, 1);
    } else if (node->flags & LYS_STATUS_OBSLT) {
        yin_print_open(out, "status", "value", "obsolete", level, 1);
    }

    if (node->dsc) {
        yin_print_text(out, level, "description", node->dsc);
    }
    if (node->ref) {
        yin_print_text(out, level, "reference", node->ref);
    }
}

static int
yin_has_snode_common2(const struct lys_node *node)
{
    if ((node->parent && (node->parent->flags & LYS_CONFIG_MASK) != (node->flags & LYS_CONFIG_MASK))
            || (!node->parent && (node->flags & LYS_CONFIG_R)) || (node->flags & LYS_MAND_MASK)) {
        return 1;
    }
    return yin_has_snode_common(node);
}

/*
 * Covers:
 * config, mandatory
 * description, reference, status
 */
static void
yin_print_snode_common2(struct lyout *out, int level, const struct lys_node *node)
{
    if (node->parent) {
        if ((node->parent->flags & LYS_CONFIG_MASK) != (node->flags & LYS_CONFIG_MASK)) {
            /* print config when it differs from the parent ... */
            if (node->flags & LYS_CONFIG_W) {
                yin_print_open(out, "config", "value", "true", level, 1);
            } else if (node->flags & LYS_CONFIG_R) {
                yin_print_open(out, "config", "value", "false", level, 1);
            }
        }
    } else if (node->flags & LYS_CONFIG_R) {
        /* ... or is a top-level state node */
        yin_print_open(out, "config", "value", "false", level, 1);
    }

    if (node->flags & LYS_MAND_TRUE) {
        yin_print_open(out, "mandatory", "value", "true", level, 1);
    } else if (node->flags & LYS_MAND_FALSE) {
        yin_print_open(out, "mandatory", "value", "false", level, 1);
    }

    yin_print_snode_common(out, level, node);
}

static void
yin_print_iffeature(struct lyout *out, int level, const struct lys_module *module, const struct lys_feature *feat)
{
    const struct lys_module *mod;

    ly_print(out, "%*s<if-feature name=\"", LEVEL, INDENT);
    mod = (feat->module->type ? ((struct lys_submodule *)feat->module)->belongsto : feat->module);
    if (module != mod) {
        ly_print(out, "%s:", transform_module_name2import_prefix(module, mod->name));
    }
    ly_print(out, "%s\"/>\n", feat->name);
}

static void
yin_print_feature(struct lyout *out, int level, const struct lys_feature *feat)
{
    int i, close;

    close = (yin_has_snode_common((struct lys_node *)feat) || feat->features_size ? 0 : 1);

    yin_print_open(out, "feature", "name", feat->name, level, close);

    if (!close) {
        level++;
        yin_print_snode_common(out, level, (struct lys_node *)feat);
        for (i = 0; i < feat->features_size; ++i) {
            yin_print_iffeature(out, level, feat->module, feat->features[i]);
        }
        level--;

        yin_print_close(out, "feature", level);
    }
}

static void
yin_print_when(struct lyout *out, int level, const struct lys_module *module, const struct lys_when *when)
{
    int close;
    const char *str;

    close = (when->dsc || when->ref ? 0 : 1);

    str = transform_json2schema(module, when->cond);
    if (!str) {
        ly_print(out, "(!error!)");
        return;
    }

    ly_print(out, "%*s<when condition=\"", LEVEL, INDENT);
    lyxml_dump_text(out, str);
    ly_print(out, "\"%s>\n", (close ? "/" : ""));

    lydict_remove(module->ctx, str);

    if (!close) {
        level++;
        if (when->dsc) {
            yin_print_text(out, level, "description", when->dsc);
        }
        if (when->ref) {
            yin_print_text(out, level, "reference", when->ref);
        }
        level--;

        yin_print_close(out, "when", level);
    }
}

static void
yin_print_type(struct lyout *out, int level, const struct lys_module *module, const struct lys_type *type)
{
    int i, close;
    const char *str;
    struct lys_module *mod;

    switch (type->base) {
    case LY_TYPE_BINARY:
        close = 1;
        if (type->info.binary.length) {
            close = 0;
        }
        break;
    case LY_TYPE_DEC64:
    case LY_TYPE_ENUM:
    case LY_TYPE_IDENT:
    case LY_TYPE_BITS:
    case LY_TYPE_UNION:
    case LY_TYPE_LEAFREF:
        close = 0;
        break;
    case LY_TYPE_INST:
        close = 1;
        if (type->info.inst.req) {
            close = 0;
        }
        break;
    case LY_TYPE_INT8:
    case LY_TYPE_INT16:
    case LY_TYPE_INT32:
    case LY_TYPE_INT64:
    case LY_TYPE_UINT8:
    case LY_TYPE_UINT16:
    case LY_TYPE_UINT32:
    case LY_TYPE_UINT64:
        close = 1;
        if (type->info.num.range) {
            close = 0;
        }
        break;
    case LY_TYPE_STRING:
        close = 1;
        if (type->info.str.length || type->info.str.pat_count) {
            close = 0;
        }
        break;
    default:
        close = 1;
        break;
    }

    if (type->module_name) {
        ly_print(out, "%*s<type name=\"%s:%s\"%s>\n", LEVEL, INDENT,
                 transform_module_name2import_prefix(module, type->module_name), type->der->name, (close ? "/" : ""));
    } else {
        yin_print_open(out, "type", "name", type->der->name, level, close);
    }

    if (!close) {
        level++;
        switch (type->base) {
        case LY_TYPE_BINARY:
            if (type->info.binary.length) {
                yin_print_restr(out, "length", type->info.binary.length, level);
            }
            break;
        case LY_TYPE_BITS:
            for (i = 0; i < type->info.bits.count; ++i) {
                yin_print_open(out, "bit", "name", type->info.bits.bit[i].name, level, 0);

                level++;
                yin_print_snode_common(out, level, (struct lys_node *)&type->info.bits.bit[i]);
                yin_print_unsigned(out, "position", "value", type->info.bits.bit[i].pos, level);
                level--;

                yin_print_close(out, "bit", level);
            }
            break;
        case LY_TYPE_DEC64:
            yin_print_unsigned(out, "fraction-digits", "value", type->info.dec64.dig, level);
            if (type->info.dec64.range) {
                yin_print_restr(out, "range", type->info.dec64.range, level);
            }
            break;
        case LY_TYPE_ENUM:
            for (i = 0; i < type->info.enums.count; i++) {
                yin_print_open(out, "enum", "name", type->info.enums.enm[i].name, level, 0);

                level++;
                yin_print_snode_common(out, level, (struct lys_node *)&type->info.enums.enm[i]);
                ly_print(out, "%*s<value value=\"%d\"/>\n", LEVEL, INDENT, type->info.enums.enm[i].value);
                level--;

                yin_print_close(out, "enum", level);
            }
            break;
        case LY_TYPE_IDENT:
            mod = type->info.ident.ref->module->type ?
                            ((struct lys_submodule *)type->info.ident.ref->module)->belongsto :
                            type->info.ident.ref->module;
            if (module == mod) {
                ly_print(out, "%*s<base name=\"%s\"/>\n", LEVEL, INDENT, type->info.ident.ref->name);
            } else {
                ly_print(out, "%*s<base name=\"%s:%s\"/>\n", LEVEL, INDENT,
                         transform_module_name2import_prefix(module, mod->name), type->info.ident.ref->name);
            }
            break;
        case LY_TYPE_INST:
            if (type->info.inst.req == 1) {
                yin_print_open(out, "require-instance", "value", "true", level, 1);
            } else if (type->info.inst.req == -1) {
                yin_print_open(out, "require-instance", "value", "false", level, 1);
            }
            break;
        case LY_TYPE_INT8:
        case LY_TYPE_INT16:
        case LY_TYPE_INT32:
        case LY_TYPE_INT64:
        case LY_TYPE_UINT8:
        case LY_TYPE_UINT16:
        case LY_TYPE_UINT32:
        case LY_TYPE_UINT64:
            if (type->info.num.range) {
                yin_print_restr(out, "range", type->info.num.range, level);
            }
            break;
        case LY_TYPE_LEAFREF:
            str = transform_json2schema(module, type->info.lref.path);
            yin_print_open(out, "path", "value", str, level, 1);
            lydict_remove(module->ctx, str);
            break;
        case LY_TYPE_STRING:
            if (type->info.str.length) {
                yin_print_restr(out, "length", type->info.str.length, level);
            }
            for (i = 0; i < type->info.str.pat_count; i++) {
                yin_print_restr(out, "pattern", &type->info.str.patterns[i], level);
            }
            break;
        case LY_TYPE_UNION:
            for (i = 0; i < type->info.uni.count; ++i) {
                yin_print_type(out, level, module, &type->info.uni.types[i]);
            }
            break;
        default:
            /* other types do not have substatements */
            break;
        }
        level--;

        yin_print_close(out, "type", level);
    }
}

static void
yin_print_must(struct lyout *out, int level, const struct lys_module *module, const struct lys_restr *must)
{
    const char *str;
    int close;

    close = (must->dsc || must->ref || must->eapptag || must->emsg ? 0 : 1);

    str = transform_json2schema(module, must->expr);
    if (!str) {
        ly_print(out, "(!error!)");
        return;
    }

    ly_print(out, "%*s<must condition=\"", LEVEL, INDENT);
    lyxml_dump_text(out, str);
    ly_print(out, "\"%s>\n", (close ? "/" : ""));

    lydict_remove(module->ctx, str);

    if (!close) {
        yin_print_restr_sub(out, must, level + 1);
        yin_print_close(out, "must", level);
    }
}

static void
yin_print_unique(struct lyout *out, int level, const struct lys_unique *uniq)
{
    int i;

    ly_print(out, "%*s<unique tag=\"", LEVEL, INDENT);
    for (i = 0; i < uniq->expr_size; i++) {
        ly_print(out, "%s%s", uniq->expr[i], i + 1 < uniq->expr_size ? " " : "");
    }
    ly_print(out, "\"/>\n");
}

static void
yin_print_refine(struct lyout *out, int level, const struct lys_module *module, const struct lys_refine *refine)
{
    int i;
    const char *str;

    str = transform_json2xml(module, refine->target_name, NULL, NULL, NULL);
    ly_print(out, "refine", "target-node", str, level, 0);
    lydict_remove(module->ctx, str);

    level++;
    if (refine->flags & LYS_CONFIG_W) {
        yin_print_open(out, "config", "value", "true", level, 1);
    } else if (refine->flags & LYS_CONFIG_R) {
        yin_print_open(out, "config", "value", "false", level, 1);
    }

    if (refine->flags & LYS_MAND_TRUE) {
        yin_print_open(out, "mandatory", "value", "true", level, 1);
    } else if (refine->flags & LYS_MAND_FALSE) {
        yin_print_open(out, "mandatory", "value", "false", level, 1);
    }

    yin_print_snode_common(out, level, (struct lys_node *)refine);

    for (i = 0; i < refine->must_size; ++i) {
        yin_print_must(out, level, module, &refine->must[i]);
    }

    if (refine->target_type & (LYS_LEAF | LYS_CHOICE)) {
        if (refine->mod.dflt) {
            yin_print_open(out, "default", "value", refine->mod.dflt, level, 1);
        }
    } else if (refine->target_type == LYS_CONTAINER) {
        if (refine->mod.presence) {
            yin_print_open(out, "presence", "value", refine->mod.presence, level, 1);
        }
    } else if (refine->target_type & (LYS_LIST | LYS_LEAFLIST)) {
        if (refine->flags & 0x04) {
            yin_print_unsigned(out, "min-elements", "value", refine->mod.list.min, level);
        }
        if (refine->flags & 0x08) {
            if (refine->mod.list.max) {
                yin_print_unsigned(out, "max-elements", "value", refine->mod.list.max, level);
            } else {
                yin_print_open(out, "max-elements", "value", "unbounded", level, 1);
            }
        }
    }
    level--;

    yin_print_close(out, "refine", level);
}

static void
yin_print_deviation(struct lyout *out, int level, const struct lys_module *module,
                    const struct lys_deviation *deviation)
{
    int i, j;
    const char *str;

    str = transform_json2xml(module, deviation->target_name, NULL, NULL, NULL);
    yin_print_open(out, "deviation", "target-node", str, level, 0);
    lydict_remove(module->ctx, str);

    level++;
    if (deviation->dsc) {
        yin_print_text(out, level, "description", deviation->dsc);
    }
    if (deviation->ref) {
        yin_print_text(out, level, "reference", deviation->ref);
    }

    for (i = 0; i < deviation->deviate_size; ++i) {
        ly_print(out, "%*s<deviate value=", LEVEL, INDENT);
        if (deviation->deviate[i].mod == LY_DEVIATE_NO) {
            ly_print(out, "\"not-supported\">\n");
        } else if (deviation->deviate[i].mod == LY_DEVIATE_ADD) {
            ly_print(out, "\"add\">\n");
        } else if (deviation->deviate[i].mod == LY_DEVIATE_RPL) {
            ly_print(out, "\"replace\">\n");
        } else if (deviation->deviate[i].mod == LY_DEVIATE_DEL) {
            ly_print(out, "\"delete\">\n");
        }

        level++;
        if (deviation->deviate[i].flags & LYS_CONFIG_W) {
            yin_print_open(out, "config", "value", "true", level, 1);
        } else if (deviation->deviate[i].flags & LYS_CONFIG_R) {
            yin_print_open(out, "config", "value", "false", level, 1);
        }

        if (deviation->deviate[i].flags & LYS_MAND_TRUE) {
            yin_print_open(out, "mandatory", "value", "true", level, 1);
        } else if (deviation->deviate[i].flags & LYS_MAND_FALSE) {
            yin_print_open(out, "mandatory", "value", "false", level, 1);
        }

        if (deviation->deviate[i].dflt) {
            yin_print_open(out, "default", "value", deviation->deviate[i].dflt, level, 1);
        }

        if (deviation->deviate[i].min_set) {
            yin_print_unsigned(out, "min-elements", "value", deviation->deviate[i].min, level);
        }
        if (deviation->deviate[i].max_set) {
            if (deviation->deviate[1].max) {
                yin_print_unsigned(out, "max-elements", "value", deviation->deviate[1].max, level);
            } else {
                yin_print_open(out, "max-elements", "value", "unbounded", level, 1);
            }
        }

        for (j = 0; j < deviation->deviate[i].must_size; ++j) {
            yin_print_must(out, level, module, &deviation->deviate[i].must[j]);
        }

        for (j = 0; j < deviation->deviate[i].unique_size; ++j) {
            yin_print_unique(out, level, &deviation->deviate[i].unique[j]);
        }

        if (deviation->deviate[i].type) {
            yin_print_type(out, level, module, deviation->deviate[i].type);
        }

        if (deviation->deviate[i].units) {
            yin_print_open(out, "units", "name", deviation->deviate[i].units, level, 1);
        }
        level--;

        yin_print_close(out, "deviate", level);
    }
    level--;

    yin_print_close(out, "deviation", level);
}

static void
yin_print_augment(struct lyout *out, int level, const struct lys_module *module,
                  const struct lys_node_augment *augment)
{
    int i;
    struct lys_node *sub;
    const char *str;

    str = transform_json2xml(module, augment->target_name, NULL, NULL, NULL);
    yin_print_open(out, "augment", "target-node", str, level, 0);
    lydict_remove(module->ctx, str);

    level++;
    yin_print_nacmext(out, level, (struct lys_node *)augment, module);
    yin_print_snode_common(out, level, (struct lys_node *)augment);

    for (i = 0; i < augment->features_size; i++) {
        yin_print_iffeature(out, level, module, augment->features[i]);
    }

    if (augment->when) {
        yin_print_when(out, level, module, augment->when);
    }

    LY_TREE_FOR(augment->child, sub) {
        yin_print_snode(out, level, sub,
                        LYS_CHOICE | LYS_CONTAINER | LYS_LEAF | LYS_LEAFLIST | LYS_LIST |
                        LYS_USES | LYS_ANYXML | LYS_CASE);
    }
    level--;

    yin_print_close(out, "augment", level);
}

static void
yin_print_typedef(struct lyout *out, int level, const struct lys_module *module, const struct lys_tpdf *tpdf)
{
    yin_print_open(out, "typedef", "name", tpdf->name, level, 0);

    level++;
    yin_print_snode_common(out, level, (struct lys_node *)tpdf);
    yin_print_type(out, level, module, &tpdf->type);
    if (tpdf->units) {
        yin_print_open(out, "units", "name", tpdf->units, level, 1);
    }
    if (tpdf->dflt) {
        yin_print_open(out, "default", "value", tpdf->dflt, level, 1);
    }
    level--;

    yin_print_close(out, "typedef", level);
}

static void
yin_print_identity(struct lyout *out, int level, const struct lys_ident *ident)
{
    int close;
    const struct lys_module *mod;

    close = (yin_has_snode_common((struct lys_node *)ident) || ident->base ? 0 : 1);

    yin_print_open(out, "identity", "name", ident->name, level, close);

    if (!close) {
        level++;
        yin_print_snode_common(out, level, (struct lys_node *)ident);
        if (ident->base) {
            ly_print(out, "%*s<base name=\"", LEVEL, INDENT);
            mod = (ident->base->module->type ? ((struct lys_submodule *)ident->base->module)->belongsto : ident->base->module);
            if (ident->module != mod) {
                ly_print(out, "%s:", transform_module_name2import_prefix(ident->module, mod->name));
            }
            ly_print(out, "%s\"/>\n", ident->base->name);
        }
        level--;

        yin_print_close(out, "identity", level);
    }
}

static void
yin_print_container(struct lyout *out, int level, const struct lys_node *node)
{
    int i;
    struct lys_node *sub;
    struct lys_node_container *cont = (struct lys_node_container *)node;

    yin_print_open(out, "container", "name", node->name, level, 0);

    level++;
    yin_print_nacmext(out, level, node, node->module);

    if (cont->when) {
        yin_print_when(out, level, node->module, cont->when);
    }

    for (i = 0; i < cont->features_size; i++) {
        yin_print_iffeature(out, level, node->module, cont->features[i]);
    }

    for (i = 0; i < cont->must_size; i++) {
        yin_print_must(out, level, node->module, &cont->must[i]);
    }

    if (cont->presence) {
        yin_print_open(out, "presence", "value", cont->presence, level, 1);
    }

    yin_print_snode_common2(out, level, node);

    for (i = 0; i < cont->tpdf_size; i++) {
        yin_print_typedef(out, level, node->module, &cont->tpdf[i]);
    }

    LY_TREE_FOR(node->child, sub) {
        /* augment and data from submodules */
        if (sub->module != node->module) {
            continue;
        }
        yin_print_snode(out, level, sub,
                        LYS_CHOICE | LYS_CONTAINER | LYS_LEAF | LYS_LEAFLIST | LYS_LIST |
                        LYS_USES | LYS_GROUPING | LYS_ANYXML);
    }
    level--;

    yin_print_close(out, "container", level);
}

static void
yin_print_case(struct lyout *out, int level, const struct lys_node *node)
{
    int i;
    struct lys_node *sub;
    struct lys_node_case *cas = (struct lys_node_case *)node;

    yin_print_open(out, "case", "name", cas->name, level, 0);

    level++;
    yin_print_nacmext(out, level, node, node->module);
    yin_print_snode_common2(out, level, node);

    for (i = 0; i < cas->features_size; i++) {
        yin_print_iffeature(out, level, node->module, cas->features[i]);
    }

    if (cas->when) {
        yin_print_when(out, level, node->module, cas->when);
    }

    LY_TREE_FOR(node->child, sub) {
        /* augment and data from submodules */
        if (sub->module != node->module) {
            continue;
        }
        yin_print_snode(out, level, sub,
                        LYS_CHOICE | LYS_CONTAINER | LYS_LEAF | LYS_LEAFLIST | LYS_LIST |
                        LYS_USES | LYS_ANYXML);
    }
    level--;

    yin_print_close(out, "case", level);
}

static void
yin_print_choice(struct lyout *out, int level, const struct lys_node *node)
{
    int i;
    struct lys_node *sub;
    struct lys_node_choice *choice = (struct lys_node_choice *)node;

    yin_print_open(out, "choice", "name", node->name, level, 0);

    level++;
    yin_print_nacmext(out, level, node, node->module);
    if (choice->dflt) {
        yin_print_open(out, "default", "value", choice->dflt->name, level, 1);
    }

    yin_print_snode_common2(out, level, node);

    for (i = 0; i < choice->features_size; i++) {
        yin_print_iffeature(out, level, node->module, choice->features[i]);
    }

    if (choice->when) {
        yin_print_when(out, level, node->module, choice->when);
    }

    LY_TREE_FOR(node->child, sub) {
        /* augment and data from submodules */
        if (sub->module != node->module) {
                continue;
        }
        yin_print_snode(out, level, sub,
                        LYS_CONTAINER | LYS_LEAF | LYS_LEAFLIST | LYS_LIST | LYS_ANYXML | LYS_CASE);
    }
    level--;

    yin_print_close(out, "choice", level);
}

static void
yin_print_leaf(struct lyout *out, int level, const struct lys_node *node)
{
    int i;
    struct lys_node_leaf *leaf = (struct lys_node_leaf *)node;

    yin_print_open(out, "leaf", "name", node->name, level, 0);

    level++;
    yin_print_nacmext(out, level, node, node->module);
    if (leaf->when) {
        yin_print_when(out, level, node->module, leaf->when);
    }
    for (i = 0; i < leaf->features_size; i++) {
        yin_print_iffeature(out, level, node->module, leaf->features[i]);
    }
    for (i = 0; i < leaf->must_size; i++) {
        yin_print_must(out, level, node->module, &leaf->must[i]);
    }
    yin_print_snode_common2(out, level, node);
    yin_print_type(out, level, node->module, &leaf->type);
    if (leaf->units) {
        yin_print_open(out, "units", "name", leaf->units, level, 1);
    }
    if (leaf->dflt) {
        yin_print_open(out, "default", "value", leaf->dflt, level, 1);
    }
    level--;

    yin_print_close(out, "leaf", level);
}

static void
yin_print_anyxml(struct lyout *out, int level, const struct lys_node *node)
{
    int i, close;
    struct lys_node_anyxml *anyxml = (struct lys_node_anyxml *)node;

    close = (yin_has_nacmext(node) || yin_has_snode_common2(node) || anyxml->features_size || anyxml->must_size
            || anyxml->when ? 0 : 1);

    yin_print_open(out, "anyxml", "name", anyxml->name, level, close);

    if (!close) {
        level++;
        yin_print_nacmext(out, level, node, node->module);
        yin_print_snode_common2(out, level, node);
        for (i = 0; i < anyxml->features_size; i++) {
            yin_print_iffeature(out, level, node->module, anyxml->features[i]);
        }
        for (i = 0; i < anyxml->must_size; i++) {
            yin_print_must(out, level, node->module, &anyxml->must[i]);
        }
        if (anyxml->when) {
            yin_print_when(out, level, node->module, anyxml->when);
        }
        level--;

        yin_print_close(out, "anyxml", level);
    }
}

static void
yin_print_leaflist(struct lyout *out, int level, const struct lys_node *node)
{
    int i;
    struct lys_node_leaflist *llist = (struct lys_node_leaflist *)node;

    yin_print_open(out, "leaf-list", "name", node->name, level, 0);

    level++;
    yin_print_nacmext(out, level, node, node->module);
    if (llist->when) {
        yin_print_when(out, level, llist->module, llist->when);
    }
    for (i = 0; i < llist->features_size; i++) {
        yin_print_iffeature(out, level, node->module, llist->features[i]);
    }
    for (i = 0; i < llist->must_size; i++) {
        yin_print_must(out, level, node->module, &llist->must[i]);
    }
    yin_print_snode_common2(out, level, node);
    yin_print_type(out, level, node->module, &llist->type);
    if (llist->units) {
        yin_print_open(out, "units", "name", llist->units, level, 1);
    }
    if (llist->min > 0) {
        yin_print_unsigned(out, "min-elements", "value", llist->min, level);
    }
    if (llist->max > 0) {
        yin_print_unsigned(out, "max-elements", "value", llist->max, level);
    }
    if (llist->flags & LYS_USERORDERED) {
        yin_print_open(out, "ordered-by", "value", "user", level, 1);
    }
    level--;

    yin_print_close(out, "leaf-list", level);
}

static void
yin_print_list(struct lyout *out, int level, const struct lys_node *node)
{
    int i;
    struct lys_node *sub;
    struct lys_node_list *list = (struct lys_node_list *)node;

    yin_print_open(out, "list", "name", node->name, level, 0);

    level++;
    yin_print_nacmext(out, level, node, node->module);
    if (list->when) {
        yin_print_when(out, level, list->module, list->when);
    }
    for (i = 0; i < list->features_size; i++) {
        yin_print_iffeature(out, level, node->module, list->features[i]);
    }
    for (i = 0; i < list->must_size; i++) {
        yin_print_must(out, level, list->module, &list->must[i]);
    }
    if (list->keys_size) {
        ly_print(out, "%*s<key value=\"", LEVEL, INDENT);
        for (i = 0; i < list->keys_size; i++) {
            ly_print(out, "%s%s", list->keys[i]->name, i + 1 < list->keys_size ? " " : "");
        }
        ly_print(out, "\"/>\n");
    }
    for (i = 0; i < list->unique_size; i++) {
        yin_print_unique(out, level, &list->unique[i]);
    }
    yin_print_snode_common2(out, level, node);
    if (list->min > 0) {
        yin_print_unsigned(out, "min-elements", "value", list->min, level);
    }
    if (list->max > 0) {
        yin_print_unsigned(out, "max-elements", "value", list->max, level);
    }
    if (list->flags & LYS_USERORDERED) {
        yin_print_open(out, "ordered-by", "value", "user", level, 1);
    }

    for (i = 0; i < list->tpdf_size; i++) {
        yin_print_typedef(out, level, list->module, &list->tpdf[i]);
    }
    LY_TREE_FOR(node->child, sub) {
        /* augment and data from submodules */
        if (sub->module != node->module) {
            continue;
        }
        yin_print_snode(out, level, sub,
                        LYS_CHOICE | LYS_CONTAINER | LYS_LEAF | LYS_LEAFLIST | LYS_LIST |
                        LYS_USES | LYS_GROUPING | LYS_ANYXML);
    }
    level--;

    yin_print_close(out, "list", level);
}

static void
yin_print_grouping(struct lyout *out, int level, const struct lys_node *node)
{
    int i;
    struct lys_node *child;
    struct lys_node_grp *grp = (struct lys_node_grp *)node;

    yin_print_open(out, "grouping", "name", node->name, level, 0);

    level++;
    yin_print_snode_common(out, level, node);

    for (i = 0; i < grp->tpdf_size; i++) {
        yin_print_typedef(out, level, node->module, &grp->tpdf[i]);
    }

    LY_TREE_FOR(node->child, child) {
        yin_print_snode(out, level, child,
                        LYS_CHOICE | LYS_CONTAINER | LYS_LEAF | LYS_LEAFLIST | LYS_LIST |
                        LYS_USES | LYS_GROUPING | LYS_ANYXML);
    }
    level--;

    yin_print_close(out, "grouping", level);
}

static void
yin_print_uses(struct lyout *out, int level, const struct lys_node *node)
{
    int i, close;
    struct lys_node_uses *uses = (struct lys_node_uses *)node;
    const struct lys_module *mod;

    close = (yin_has_nacmext(node) || yin_has_snode_common(node) || uses->features_size || uses->when
            || uses->refine_size || uses->augment_size ? 0 : 1);

    ly_print(out, "%*s<uses name=\"", LEVEL, INDENT);
    if (node->child) {
        mod = (node->child->module->type ? ((struct lys_submodule *)node->child->module)->belongsto : node->child->module);
        if (node->module != mod) {
            ly_print(out, "%s:", transform_module_name2import_prefix(node->module, mod->name));
        }
    }
    ly_print(out, "%s\"%s>\n", uses->name, (close ? "/" : ""));

    if (!close) {
        level++;
        yin_print_nacmext(out, level, node, node->module);
        yin_print_snode_common(out, level, node);
        for (i = 0; i < uses->features_size; i++) {
            yin_print_iffeature(out, level, node->module, uses->features[i]);
        }
        if (uses->when) {
            yin_print_when(out, level, node->module, uses->when);
        }

        for (i = 0; i < uses->refine_size; i++) {
            yin_print_refine(out, level, node->module, &uses->refine[i]);
        }

        for (i = 0; i < uses->augment_size; i++) {
            yin_print_augment(out, level, node->module, &uses->augment[i]);
        }
        level--;

        yin_print_close(out, "uses", level);
    }
}

static void
yin_print_input_output(struct lyout *out, int level, const struct lys_node *node)
{
    int i;
    struct lys_node *sub;
    struct lys_node_rpc_inout *inout = (struct lys_node_rpc_inout *)node;

    ly_print(out, "%*s<%s>\n", LEVEL, INDENT, (inout->nodetype == LYS_INPUT ? "input" : "output"));

    level++;
    for (i = 0; i < inout->tpdf_size; i++) {
        yin_print_typedef(out, level, node->module, &inout->tpdf[i]);
    }

    LY_TREE_FOR(node->child, sub) {
        /* augment and data from submodules */
        if (sub->module != node->module) {
            continue;
        }
        yin_print_snode(out, level, sub,
                        LYS_CHOICE | LYS_CONTAINER | LYS_LEAF | LYS_LEAFLIST | LYS_LIST |
                        LYS_USES | LYS_GROUPING | LYS_ANYXML);
    }
    level--;

    yin_print_close(out, (inout->nodetype == LYS_INPUT ? "input" : "output"), level);
}

static void
yin_print_rpc(struct lyout *out, int level, const struct lys_node *node)
{
    int i, close;
    struct lys_node *sub;
    struct lys_node_rpc *rpc = (struct lys_node_rpc *)node;

    close = (yin_has_snode_common(node) || rpc->features_size || rpc->tpdf_size || node->child ? 0 : 1);

    yin_print_open(out, "rpc", "name", node->name, level, close);

    if (!close) {
        level++;
        yin_print_snode_common(out, level, node);

        for (i = 0; i < rpc->features_size; i++) {
            yin_print_iffeature(out, level, node->module, rpc->features[i]);
        }

        for (i = 0; i < rpc->tpdf_size; i++) {
            yin_print_typedef(out, level, node->module, &rpc->tpdf[i]);
        }

        LY_TREE_FOR(node->child, sub) {
            /* augment and data from submodules */
            if (sub->module != node->module) {
                continue;
            }
            yin_print_snode(out, level, sub, LYS_GROUPING | LYS_INPUT | LYS_OUTPUT);
        }
        level--;

        yin_print_close(out, "rpc", level);
    }
}

static void
yin_print_notif(struct lyout *out, int level, const struct lys_node *node)
{
    int i, close;
    struct lys_node *sub;
    struct lys_node_notif *notif = (struct lys_node_notif *)node;

    close = (yin_has_snode_common(node) || notif->features_size || notif->tpdf_size || node->child ? 0 : 1);

    yin_print_open(out, "notification", "name", node->name, level, close);

    if (!close) {
        level++;
        yin_print_snode_common(out, level, node);

        for (i = 0; i < notif->features_size; i++) {
            yin_print_iffeature(out, level, node->module, notif->features[i]);
        }

        for (i = 0; i < notif->tpdf_size; i++) {
            yin_print_typedef(out, level, node->module, &notif->tpdf[i]);
        }

        LY_TREE_FOR(node->child, sub) {
            /* augment and data from submodules */
            if (sub->module != node->module) {
                continue;
            }
            yin_print_snode(out, level, sub,
                            LYS_CHOICE | LYS_CONTAINER | LYS_LEAF | LYS_LEAFLIST | LYS_LIST |
                            LYS_USES | LYS_GROUPING | LYS_ANYXML);
        }
        level--;

        yin_print_close(out, "notification", level);
    }
}

static void
yin_print_snode(struct lyout *out, int level, const struct lys_node *node, int mask)
{
    switch (node->nodetype & mask) {
    case LYS_CONTAINER:
        yin_print_container(out, level, node);
        break;
    case LYS_CHOICE:
        yin_print_choice(out, level, node);
        break;
    case LYS_LEAF:
        yin_print_leaf(out, level, node);
        break;
    case LYS_LEAFLIST:
        yin_print_leaflist(out, level, node);
        break;
    case LYS_LIST:
        yin_print_list(out, level, node);
        break;
    case LYS_USES:
        yin_print_uses(out, level, node);
        break;
    case LYS_GROUPING:
        yin_print_grouping(out, level, node);
        break;
    case LYS_ANYXML:
        yin_print_anyxml(out, level, node);
        break;
    case LYS_CASE:
        yin_print_case(out, level, node);
        break;
    case LYS_INPUT:
    case LYS_OUTPUT:
        yin_print_input_output(out, level, node);
        break;
    default:
        break;
    }
}

static void
yin_print_namespaces(struct lyout *out, const struct lys_module *module)
{
    unsigned int i, lvl;

    if (module->type) {
        lvl = 11;
    } else {
        lvl = 8;
    }

    ly_print(out, "%*sxmlns=\"%s\"", lvl, INDENT, LY_NSYIN);
    if (!module->type) {
        ly_print(out, "\n%*sxmlns:%s=\"%s\"", lvl, INDENT, module->prefix, module->ns);
    }
    for (i = 0; i < module->imp_size; ++i) {
        if (module->imp[i].external) {
            continue;
        }
        ly_print(out, "\n%*sxmlns:%s=\"%s\"", lvl, INDENT, module->imp[i].prefix, module->imp[i].module->ns);
    }
}

int
yin_print_model(struct lyout *out, const struct lys_module *module)
{
    unsigned int i;
    int level = 0, close;
#define LEVEL (level*2)

    struct lys_node *node;

    ly_print(out, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");

    if (module->deviated) {
        ly_print(out, "<!-- DEVIATED -->\n");
    }

    /* (sub)module-header-stmts */
    if (module->type) {
        ly_print(out, "<submodule name=\"%s\"\n", module->name);
        yin_print_namespaces(out, module);
        ly_print(out, ">\n");

        level++;
        if (module->version) {
            yin_print_open(out, "yang-version", "value",
                           ((struct lys_submodule *)module)->belongsto->version == 2 ? "1.1" : "1", level, 1);
        }
        yin_print_open(out, "belongs-to", "module", ((struct lys_submodule *)module)->belongsto->name, level, 0);

        level++;
        yin_print_open(out, "prefix", "value", module->prefix, level, 1);
        level--;

        yin_print_close(out, "belongs-to", level);
    } else {
        ly_print(out, "<module name=\"%s\"\n", module->name);
        yin_print_namespaces(out, module);
        ly_print(out, ">\n");

        level++;
        if (module->version) {
            yin_print_open(out, "yang-version", "value", module->version == 2 ? "1.1" : "1", level, 1);
        }
        yin_print_open(out, "namespace", "uri", module->ns, level, 1);
        yin_print_open(out, "prefix", "value", module->prefix, level, 1);
    }

    /* linkage-stmts */
    for (i = 0; i < module->imp_size; i++) {
        if (module->imp[i].external) {
            continue;
        }
        yin_print_open(out, "import", "module", module->imp[i].module->name, level, 0);

        level++;
        yin_print_open(out, "prefix", "value", module->imp[i].prefix, level, 1);
        if (module->imp[i].rev[0]) {
            yin_print_open(out, "revision-date", "date", module->imp[i].rev, level, 1);
        }
        level--;

        yin_print_close(out, "import", level);
    }
    for (i = 0; i < module->inc_size; i++) {
        if (module->inc[i].external) {
            continue;
        }

        close = (module->inc[i].rev[0] ? 0 : 1);
        yin_print_open(out, "include", "value", module->inc[i].submodule->name, level, close);

        if (!close) {
            level++;
            yin_print_open(out, "revision-date", "date", module->inc[i].rev, level, 1);
            level--;

            yin_print_close(out, "include", level);
        }
    }

    /* meta-stmts */
    if (module->org) {
        yin_print_text(out, level, "organization", module->org);
    }
    if (module->contact) {
        yin_print_text(out, level, "contact", module->contact);
    }
    if (module->dsc) {
        yin_print_text(out, level, "description", module->dsc);
    }
    if (module->ref) {
        yin_print_text(out, level, "reference", module->ref);
    }

    /* revision-stmts */
    for (i = 0; i < module->rev_size; i++) {
        close = (module->rev[i].dsc || module->rev[i].ref ? 0 : 1);
        yin_print_open(out, "revision", "date", module->rev[i].date, level, close);

        if (!close) {
            level++;
            if (module->rev[i].dsc) {
                yin_print_text(out, level, "description", module->rev[i].dsc);
            }
            if (module->rev[i].ref) {
                yin_print_text(out, level, "reference", module->rev[i].ref);
            }
            level--;

            yin_print_close(out, "revision", level);
        }
    }

    /* body-stmts */
    for (i = 0; i < module->features_size; i++) {
        yin_print_feature(out, level, &module->features[i]);
    }

    for (i = 0; i < module->ident_size; i++) {
        yin_print_identity(out, level, &module->ident[i]);
    }

    for (i = 0; i < module->tpdf_size; i++) {
        yin_print_typedef(out, level, module, &module->tpdf[i]);
    }

    for (i = 0; i < module->deviation_size; ++i) {
        yin_print_deviation(out, level, module, &module->deviation[i]);
    }

    LY_TREE_FOR(module->data, node) {
        if (node->module != module) {
            continue;
        }

        switch (node->nodetype) {
        case LYS_RPC:
            yin_print_rpc(out, level, node);
            break;
        case LYS_NOTIF:
            yin_print_notif(out, level, node);
            break;
        default:
            yin_print_snode(out, level, node,
                             LYS_CHOICE | LYS_CONTAINER | LYS_LEAF | LYS_LEAFLIST | LYS_LIST |
                             LYS_USES | LYS_GROUPING | LYS_ANYXML);
            break;
        }
    }

    for (i = 0; i < module->augment_size; i++) {
        yin_print_augment(out, level, module, &module->augment[i]);
    }

    if (module->type) {
        ly_print(out, "</submodule>\n");
    } else {
        ly_print(out, "</module>\n");
    }

    return EXIT_SUCCESS;
#undef LEVEL
}

