package it.yasde;

import java.util.concurrent.BlockingQueue;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

/**
 * Barriera su una coda di esecuzione (senza Android, provabile sulla JVM): mette in coda `lavoro` e
 * aspetta, con limite, che il thread della coda l'abbia eseguito; poiche' la coda e' FIFO, tutto cio'
 * che c'era prima e' stato servito. Se la coda e' piena o il thread e' fermo non si aspetta oltre il
 * limite: il lavoro, se messo in coda, viene eseguito appena il thread riparte.
 */
final class Barriera {
    private Barriera() {}

    /** Ritorna true se `lavoro` e' stato eseguito entro i limiti. */
    static boolean esegui(BlockingQueue<Runnable> coda, final Runnable lavoro, long entraMs, long attesaMs)
            throws InterruptedException {
        final CountDownLatch fatto = new CountDownLatch(1);
        boolean messo = coda.offer(new Runnable() {
            @Override
            public void run() {
                try {
                    lavoro.run();
                } finally {
                    fatto.countDown();
                }
            }
        }, entraMs, TimeUnit.MILLISECONDS);
        return messo && fatto.await(attesaMs, TimeUnit.MILLISECONDS);
    }
}
