// Base: https://github.com/jw1912/bullet/blob/main/examples/progression/2_output_buckets.rs
//
// Usage: train [--finetune CHECKPOINT] DATA... OUTPUT_DIR
// DATA is the bulletformat file made by src/prepare.rs.

mod input;

use bullet_lib::{
    game::outputs::MaterialCount,
    nn::optimiser::AdamW,
    trainer::{
        save::SavedFormat,
        schedule::{lr, wdl, TrainingSchedule, TrainingSteps},
        settings::LocalSettings,
    },
    value::{loader::DirectSequentialDataLoader, ValueTrainerBuilder},
};
use input::Chess768EngineOrder;

const NET_NAME: &str = "gen4-512";
const HIDDEN_SIZE: usize = 512;
const NUM_OUTPUT_BUCKETS: usize = 8;
const SCALE: f32 = 250.0;
const QA: i16 = 255;
const QB: i16 = 64;

const WDL_START: f32 = 0.3;
const WDL_END: f32 = 0.5;
const SUPERBATCHES: usize = 80;
const INITIAL_LR: f32 = 0.001;
const LR_DECAY: f32 = 0.3 * 0.3 * 0.3 * 0.3 * 0.3;
const BATCHES_PER_SUPERBATCH: usize = 6104;
const FINETUNE_SUPERBATCHES: usize = 8;
const FINETUNE_BATCHES_PER_SUPERBATCH: usize = BATCHES_PER_SUPERBATCH / 4;

const THREADS: usize = 2;

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let (checkpoint, rest) = match args.get(1).map(String::as_str) {
        Some("--finetune") => (args.get(2), &args[3.min(args.len())..]),
        _ => (None, &args[1..]),
    };
    if rest.len() < 2 {
        panic!("Usage: train [--finetune CHECKPOINT] DATA... OUTPUT_DIR");
    }
    let output_dir = &rest[rest.len() - 1];
    let data_paths = &rest[..rest.len() - 1];

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

    let steps = |superbatches, batches_per_superbatch| TrainingSteps {
        batch_size: 16_384,
        batches_per_superbatch,
        start_superbatch: 1,
        end_superbatch: superbatches,
    };

    let settings = LocalSettings {
        threads: THREADS,
        test_set: None,
        output_directory: output_dir,
        batch_queue_size: 32,
    };
    let data_loader =
        DirectSequentialDataLoader::new(&data_paths.iter().map(String::as_str).collect::<Vec<_>>());
    match checkpoint {
        None => {
            let schedule = TrainingSchedule {
                net_id: NET_NAME.to_string(),
                eval_scale: SCALE,
                steps: steps(SUPERBATCHES, BATCHES_PER_SUPERBATCH),
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
            };
            trainer.run(&schedule, &settings, &data_loader);
        }
        Some(checkpoint) => {
            trainer.load_from_checkpoint(checkpoint);
            let schedule = TrainingSchedule {
                net_id: format!("{NET_NAME}-finetune"),
                eval_scale: SCALE,
                steps: steps(FINETUNE_SUPERBATCHES, FINETUNE_BATCHES_PER_SUPERBATCH),
                wdl_scheduler: wdl::ConstantWDL { value: WDL_END },
                lr_scheduler: lr::ConstantLR {
                    value: INITIAL_LR * LR_DECAY,
                },
                save_rate: 1,
            };
            trainer.run(&schedule, &settings, &data_loader);
        }
    }
}
