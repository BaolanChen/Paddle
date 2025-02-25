# Copyright (c) 2022 PaddlePaddle Authors. All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import logging

import paddle
from paddle.optimizer.lr import (
    ExponentialDecay,
    InverseTimeDecay,
    LRScheduler,
    NaturalExpDecay,
    NoamDecay,
    exponential_decay,
    inverse_time_decay,
    noam_decay,
)

from ..ps.utils.public import (
    get_optimize_ops,
    get_ps_endpoint,
    get_role_id,
    get_trainers,
)
from .pass_base import PassBase, register_pass


@register_pass("add_lr_decay_table_pass")
class AddLrDecayTablePass(PassBase):
    def __init__(self):
        super().__init__()

    def _check_self(self):
        return True

    def _check_conflict(self, other_pass):
        return True

    def _add_tensor_table(
        self,
        attrs,
        feed_var_name,
        fetch_var_name="",
        startup_program=None,
        main_program=None,
        tensor_table_class="",
    ):
        tensor_table_dict = {}
        tensor_table_dict[feed_var_name] = {}
        tensor_table_dict[feed_var_name]["feed_var_name"] = feed_var_name
        tensor_table_dict[feed_var_name]["fetch_var_name"] = fetch_var_name
        tensor_table_dict[feed_var_name]["startup_program"] = startup_program
        tensor_table_dict[feed_var_name]["main_program"] = main_program
        tensor_table_dict[feed_var_name][
            "tensor_table_class"
        ] = tensor_table_class
        attrs['tensor_table'] = tensor_table_dict

    def _get_lr_scheduler_program(self, lr_scheduler, lr_decay_steps):
        scheduler_decay = [
            'NoamDecay',
            'NaturalExpDecay',
            'InverseTimeDecay',
            'ExponentialDecay',
        ]

        decay_main_program = paddle.static.Program()
        decay_startup_program = paddle.static.Program()
        lr_name = ""

        if isinstance(lr_scheduler, ExponentialDecay):
            with paddle.static.program_guard(
                decay_main_program, decay_startup_program
            ):
                lr = exponential_decay(
                    1.0, lr_decay_steps, lr_scheduler.gamma, True
                )
                lr_name = lr.name
                logging.warning(
                    f"ExponentialDecay is set, staircase = True, global learning rate decay step is [ {lr_decay_steps} ], Change decay steps as follow: \n"
                    "\t strategy = paddle.distributed.fleet.DistributedStrategy() \n "
                    "\t strategy.a_sync = True \n"
                    "\t strategy.a_sync_configs= { 'lr_decay_steps' : YOUR_DECAY_STEP } \n"
                )
        elif isinstance(lr_scheduler, NoamDecay):
            with paddle.static.program_guard(
                decay_main_program, decay_startup_program
            ):
                lr = noam_decay(
                    lr_scheduler.d_model, lr_scheduler.warmup_steps, 1.0
                )
                lr_name = lr.name
                logging.warning(
                    f"NoamDecay is set, warmup steps is [ {lr_scheduler.warmup_steps} ]"
                )
        elif isinstance(lr_scheduler, NaturalExpDecay):
            with paddle.static.program_guard(
                decay_main_program, decay_startup_program
            ):
                lr = paddle.optimizer.lr.NaturalExpDecay(
                    1.0, lr_scheduler.gamma
                ).get_lr()
                lr_name = lr.name
                logging.warning(
                    f"NaturalExpDecay is set, staircase = True, global learning rate decay step is [ {lr_decay_steps} ], Change decay steps as follow: \n"
                    "\t strategy = paddle.distributed.fleet.DistributedStrategy() \n "
                    "\t strategy.a_sync = True \n"
                    "\t strategy.a_sync_configs= { 'lr_decay_steps' : YOUR_DECAY_STEP } \n"
                )
        elif isinstance(lr_scheduler, InverseTimeDecay):
            with paddle.static.program_guard(
                decay_main_program, decay_startup_program
            ):
                lr = inverse_time_decay(
                    1.0, lr_decay_steps, lr_scheduler.gamma, True
                )
                lr_name = lr.name
                logging.warning(
                    f"InverseTimeDecay is set, staircase = True, global learning rate decay step is [ {lr_decay_steps} ], Change decay steps as follow: \n"
                    "\t strategy = paddle.distributed.fleet.DistributedStrategy() \n "
                    "\t strategy.a_sync = True \n"
                    "\t strategy.a_sync_configs= { 'lr_decay_steps' : YOUR_DECAY_STEP } \n"
                )
        else:
            raise ValueError(
                f"Not supported current LearningRate strategy, please use follow decay strategy: {scheduler_decay}"
            )

        return decay_main_program, decay_startup_program, lr_name

    def _apply_single_impl(self, main_program, startup_program, pass_ctx):
        attrs = pass_ctx._attrs
        if not hasattr(attrs['origin_main_program'], 'lr_scheduler'):
            return

        assert isinstance(
            attrs['origin_main_program'].lr_scheduler, LRScheduler
        ), "must be LRScheduler"

        ops = get_optimize_ops(attrs['origin_main_program'])
        (
            lr_decay_main_program,
            lr_decay_startup_program,
            lr_name,
        ) = self._get_lr_scheduler_program(
            attrs['origin_main_program'].lr_scheduler, attrs['lr_decay_steps']
        )
        self._add_tensor_table(
            attrs,
            "@LR_DECAY_COUNTER@",
            lr_name,
            lr_decay_startup_program,
            lr_decay_main_program,
            "GlobalStepTable",
        )
        return


@register_pass("add_listen_and_serv_pass")
class AddListenAndServPass(PassBase):
    def __init__(self):
        super().__init__()

    def _check_self(self):
        return True

    def _check_conflict(self, other_pass):
        return True

    def _apply_single_impl(self, main_program, startup_program, pass_ctx):
        attrs = pass_ctx._attrs
        opt = {
            "grad_to_block_id": None,
            "sparse_grad_to_param": None,
            "lr_decay_block_id": None,
            "dense_optimize_blocks": None,
            "sparse_optimize_blocks": None,
            # runtime attribute
            "endpoint": get_ps_endpoint(attrs['role_maker']),
            "pserver_id": get_role_id(attrs['role_maker']),
            "Fanin": get_trainers(attrs['role_maker']),
            "distributed_mode": attrs['ps_mode'],
            "rpc_get_thread_num": -1,
            "rpc_send_thread_num": -1,
            "rpc_prefetch_thread_num": -1,
        }
        main_program.global_block().append_op(
            type="listen_and_serv", inputs={'X': []}, outputs={}, attrs=opt
        )


@register_pass("add_rpc_global_flags_pass")
class AddRpcGlobalFlagsPass(PassBase):
    def __init__(self):
        super().__init__()

    def _check_self(self):
        return True

    def _check_conflict(self, other_pass):
        return True

    def _apply_single_impl(self, main_program, startup_program, pass_ctx):
        pass


@register_pass("add_optimizer_pass")
class AddOptimizerPass(PassBase):
    def __init__(self):
        super().__init__()

    def _check_self(self):
        return True

    def _check_conflict(self, other_pass):
        return True

    def _apply_single_impl(self, main_program, startup_program, pass_ctx):
        pass


@register_pass("add_geo_optimizer_pass")
class AddGeoOptimizerPass(PassBase):
    def __init__(self):
        super().__init__()

    def _check_self(self):
        return True

    def _check_conflict(self, other_pass):
        return True

    def _apply_single_impl(self, main_program, startup_program, pass_ctx):
        pass


@register_pass("build_pserver_startup_program_pass")
class BuildPserverStartupProgramPass(PassBase):
    def __init__(self):
        super().__init__()

    def _check_self(self):
        return True

    def _check_conflict(self, other_pass):
        return True

    def _apply_single_impl(self, main_program, startup_program, pass_ctx):
        pass


@register_pass("delete_unused_in_startup_pass")
class DeleteUnusedInStartupPass(PassBase):
    def __init__(self):
        super().__init__()

    def _check_self(self):
        return True

    def _check_conflict(self, other_pass):
        return True

    def _apply_single_impl(self, main_program, startup_program, pass_ctx):
        pass
