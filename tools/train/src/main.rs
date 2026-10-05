// Base: https://github.com/jw1912/bullet/blob/main/examples/progression/2_output_buckets.rs
//
// Usage: train DATA... OUTPUT_DIR
// Several viriformat files are interleaved, so every batch mixes positions from all of them.

mod input;

use bullet_lib::{
    game::outputs::MaterialCount,
    nn::optimiser::AdamW,
    trainer::{
        save::SavedFormat,
        schedule::{lr, wdl, TrainingSchedule, TrainingSteps},
        settings::LocalSettings,
    },
    value::{
        loader::viribinpack::{Filter, ViriBinpackLoader, ViriFilter},
        ValueTrainerBuilder,
    },
};
use input::Chess768EngineOrder;

const NET_NAME: str = "net";
const HIDDEN_SIZE: usize = 512;
const NUM_OUTPUT_BUCKETS: usize = 8;
const SCALE: f32 = 261.0;
const QA: i16 = 255;
const QB: i16 = 64;

const WDL_START: f32 = 0.3;
const WDL_END: f32 = 0.5;
const SUPERBATCHES: usize = 40;
const INITIAL_LR: f32 = 0.001;
const LR_DECAY: f32 = 0.3 * 0.3 * 0.3 * 0.3 * 0.3;

const THREADS: usize = 2;
const LOADER_BUFFER_MB: usize = 1024;

fn main() {
    let args: Vec<String> = std::env::args();
    if args.len() < 3 {
        panic!("Usage: train DATA... OUTPUT_DIR");
    }
    let output_dir = &args[args.len() - 1];
    let data_paths = &args[1..args.len() - 1];

    let mut trainer = ValueTrainerBuilder::default()
        .dual_perspective()
        .optimiser(AdamW)
        .inputs(Chess768EngineOrder)
        .output_buckets(MaterialCount::<NUM_OUTPUT_BUCKETS>)
        .save_format(&[
            SavedFormat::id("l0w").round().quantise::<i16>(QA),
            SavedFormat::id("l0b").round().quantise::<i16>(QA),
            SavedFormat::id("l1w")
                .round()
                .quantise::<i16>(QB)
                .transpose(),
            SavedFormat::id("l1b").round().quantise::<i16>(QA * QB),
        ])
        .loss_fn(|output, target| output.sigmoid().squared_error(target))
        .build(|builder, stm_inputs, ntm_inputs, output_buckets| {
            let l0 = builder.new_affine("l0", 768, HIDDEN_SIZE);
            let l1 = builder.new_affine("l1", 2 * HIDDEN_SIZE, NUM_OUTPUT_BUCKETS);

            let stm_hidden = l0.forward(stm_inputs).screlu();
            let ntm_hidden = l0.forward(ntm_inputs).screlu();
            let hidden_layer = stm_hidden.concat(ntm_hidden);
            l1.forward(hidden_layer).select(output_buckets)
        });

    let schedule = TrainingSchedule {
        net_id: NET_NAME.to_string(),
        eval_scale: SCALE,
        steps: TrainingSteps {
            batch_size: 16_384,
            batches_per_superbatch: 6104,
            start_superbatch: 1,
            end_superbatch: SUPERBATCHES,
        },
        wdl_scheduler: wdl::LinearWDL {
            start: WDL_START,
            end: WDL_END,
        },
        lr_scheduler: lr::CosineDecayLR {
            initial_lr: INITIAL_LR,
            final_lr: INITIAL_LR * LR_DECAY,
            final_superbatch: SUPERBATCHES,
        },
        save_rate: 10,
    }

    let settings = LocalSettings {
        threads: THREADS,
        test_set: None,
        output_directory: output_dir,
        batch_queue_size: 32,
    };
    let data_loader = ViriBinpackLoader::new_interleave_multiple(
        &data_paths.iter().map(String::as_str).collect(),
        LOADER_BUFFER_MB,
        THREADS,
        ViriFilter::Builtin(Filter::default()),
    )
    trainer.run(&schedule(), &settings, &data_loader);
}
