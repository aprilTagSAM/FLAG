/* Copyright (C) 2013-2016, The Regents of The University of Michigan.
All rights reserved.

This software was developed in the APRIL Robotics Lab under the
direction of Edwin Olson, ebolson@umich.edu. This software may be
available under alternative licensing terms; contact the address above.

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, see <http://www.gnu.org/licenses/>.
*/

#include "april_graph.h"
#include "common/doubles.h"
#include "common/matd.h"

/////////////////////////////////////////////////////////////////////////////////////////
// XYTPos Factor

static april_graph_factor_t* xytpos_factor_copy(april_graph_factor_t *factor)
{
    assert(0);
    return NULL;

}

static april_graph_factor_eval_t* xytpos_factor_eval(april_graph_factor_t *factor, april_graph_t *graph, april_graph_factor_eval_t *eval)
{
    if (eval == NULL) {
        eval = calloc(1, sizeof(april_graph_factor_eval_t));
        eval->jacobians = calloc(2, sizeof(matd_t*)); // NB: NULL-terminated
        eval->jacobians[0] = matd_create(3, 3);
        eval->r = calloc(3, sizeof(double));
        eval->W = matd_create(3, 3);
    }

    // partial derivatives of zhat WRT node 0 [xa ya ta]
    MATD_EL(eval->jacobians[0], 0,0) = 1;
    MATD_EL(eval->jacobians[0], 1,1) = 1;
    MATD_EL(eval->jacobians[0], 2,2) = 1;

    april_graph_node_t *na;
    zarray_get(graph->nodes, factor->nodes[0], &na);

    eval->length = 3;

    eval->r[0] = factor->u.common.z[0] - na->state[0];
    eval->r[1] = factor->u.common.z[1] - na->state[1];
    eval->r[2] = mod2pi(factor->u.common.z[2] - na->state[2]);

    memcpy(eval->W->data, factor->u.common.W->data, 3*3*sizeof(double));

    // chi^2 = r'*W*r, via X = W*r
    double X[3] = { MATD_EL(eval->W, 0, 0)*eval->r[0] +
                    MATD_EL(eval->W, 0, 1)*eval->r[1] +
                    MATD_EL(eval->W, 0, 2)*eval->r[2],
                    MATD_EL(eval->W, 1, 0)*eval->r[0] +
                    MATD_EL(eval->W, 1, 1)*eval->r[1] +
                    MATD_EL(eval->W, 1, 2)*eval->r[2],
                    MATD_EL(eval->W, 2, 0)*eval->r[0] +
                    MATD_EL(eval->W, 2, 1)*eval->r[1] +
                    MATD_EL(eval->W, 2, 2)*eval->r[2] };
    eval->chi2 = eval->r[0]*X[0] + eval->r[1]*X[1] + eval->r[2]*X[2];

    return eval;
}


static void xytpos_factor_destroy(april_graph_factor_t *factor)

{
    free(factor->nodes);
    free(factor->u.common.z);
    free(factor->u.common.ztruth);
    matd_destroy(factor->u.common.W);
    free(factor);
}


static void april_graph_factor_xytpos_encode(const stype_t *stype, uint8_t *data, uint32_t *datapos, const void *obj)
{
    const april_graph_factor_t *factor = obj;

    encode_u32(data, datapos, factor->nodes[0]);

    for (int i = 0; i < 3; i++)
        encode_f64(data, datapos, factor->u.common.z[i]);

    if (factor->u.common.ztruth) {
        encode_u8(data, datapos, 1);

        for (int i = 0; i < 3; i++)
            encode_f64(data, datapos, factor->u.common.ztruth[i]);
    } else {
        encode_u8(data, datapos, 0);
    }

    for (int i = 0; i < 9; i++)
        encode_f64(data, datapos, factor->u.common.W->data[i]);

    april_graph_attr_t *attr = factor->attr;
    stype_encode_object(data, datapos, attr ? attr->stype : NULL, attr);
}

static void *april_graph_factor_xytpos_decode(const stype_t *stype, const uint8_t *data, uint32_t *datapos, uint32_t datalen)
{
    int a = decode_u32(data, datapos, datalen);

    double z[3];
    for (int i = 0; i < 3; i++)
        z[i] = decode_f64(data, datapos, datalen);

    double *ztruth = NULL;
    if (decode_u8(data, datapos, datalen)) {
        ztruth = malloc(2*sizeof(double));
        for (int i = 0; i < 3; i++)
            ztruth[i] = decode_f64(data, datapos, datalen);
    }

    matd_t *W = matd_create(3, 3);
    for (int i = 0; i < 9; i++)
        W->data[i] = decode_f64(data, datapos, datalen);

    april_graph_factor_t *factor = april_graph_factor_xytpos_create(a, z, ztruth, W);
    april_graph_attr_destroy(factor->attr);
    factor->attr = stype_decode_object(data, datapos, datalen, NULL);

    free(ztruth);
    matd_destroy(W);
    return factor;
}

const stype_t stype_april_factor_xytpos = { .name = "april_graph_factor_xytpos",
                                         .encode = april_graph_factor_xytpos_encode,
                                         .decode = april_graph_factor_xytpos_decode,
                                         .copy = NULL };

april_graph_factor_t *april_graph_factor_xytpos_create(int a, double *z, double *ztruth, matd_t *W)
{
    april_graph_factor_t *factor = calloc(1, sizeof(april_graph_factor_t));

    factor->type = APRIL_GRAPH_FACTOR_XYTPOS_TYPE;
    factor->nnodes = 1;
    factor->nodes = calloc(1, sizeof(int));
    factor->nodes[0] = a;
    factor->length = 3;

    factor->copy = xytpos_factor_copy;
    factor->eval = xytpos_factor_eval;
    factor->destroy = xytpos_factor_destroy;

    factor->u.common.z = doubles_dup(z, 3);
    factor->u.common.ztruth = doubles_dup(ztruth, 3);
    factor->u.common.W = matd_copy(W);

    factor->stype = &stype_april_factor_xytpos;
    return factor;
}


void april_graph_xytpos_stype_init()
{
    stype_register(&stype_april_factor_xytpos);
}
